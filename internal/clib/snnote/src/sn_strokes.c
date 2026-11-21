/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
/* Fixed record header: offsets are bytes from the record payload, not the
 * surrounding log's length field. */
constexpr size_t STROKE_HEADER_SIZE = 208;
constexpr size_t STROKE_PEN_OFFSET = 0;
constexpr size_t STROKE_COLOR_OFFSET = 4;
constexpr size_t STROKE_THICKNESS_OFFSET = 8;
constexpr size_t STROKE_PAGE_OFFSET = 28;
constexpr size_t STROKE_ROLE_OFFSET = 40;
constexpr size_t STROKE_LAYER_OFFSET = 44;
constexpr size_t STROKE_KIND_OFFSET = 48;
constexpr size_t STROKE_KIND_SIZE = 52;
constexpr size_t STROKE_LEFT_OFFSET = 100;
constexpr size_t STROKE_TOP_OFFSET = 104;
constexpr size_t STROKE_RIGHT_OFFSET = 116;
constexpr size_t STROKE_BOTTOM_OFFSET = 120;
constexpr size_t STROKE_RANGE_Y_OFFSET = 128;
constexpr size_t STROKE_RANGE_X_OFFSET = 132;
constexpr size_t DISABLED_STRIDE = 24;
constexpr size_t POINT_STRIDE = 8;
constexpr size_t PRESSURE_STRIDE = 2;
constexpr size_t COUNT_STRIDE = 4;
constexpr size_t CONTROL_STRIDE = 4;
constexpr size_t STATUS_RESERVED_SIZE = 40;
constexpr size_t CONTOUR_PREFIX_SIZE = 10;
constexpr size_t TAIL_ENTRY_CEILING = 1u << 20;
constexpr size_t TAIL_WIDE_STRIDE = 20;
constexpr size_t TAIL_NARROW_STRIDE = 16;
constexpr size_t TAIL_PREFIX_SIZE = 4;
constexpr size_t TAIL_PADDED_PREFIX_SIZE = 8;
constexpr size_t TAIL_RESERVED_SIZE = 13;
constexpr size_t ATTRIBUTE_FLAG_SIZE = 1;
constexpr size_t SKIPPED_ARRAY_STRIDES[] = {4, 1, 8, 4};

/* Header extent is checked once before random access. The byte reader still
 * supplies the little-endian conversion without unaligned loads. */
static uint32_t header_u32(sn_bytes bytes, size_t offset) {
  sn_reader reader = {bytes.data, bytes.size, offset, false};
  return sn_u32(&reader);
}

/* Accept either orientation of the shared sheet table for tail-layout
 * validation. */
static bool is_known_sheet(uint32_t width, uint32_t height) {
  for (size_t index = 0; sn_work(1), (index < SN_SHEET_COUNT); index++) {
    sn_size sheet = sn_sheets[index];
    if ((width == sheet.width && height == sheet.height) ||
        (height == sheet.width && width == sheet.height))
      return true;
  }
  return false;
}

/* Probe known tail strides by their sheet signature, not the format version.
 * Preserve a valid style even when its attribute string is absent. */
static void read_attributes(sn_arena* arena, sn_reader tail_reader,
                            sn_stroke* stroke) {
  size_t start = tail_reader.pos, entries = sn_u32(&tail_reader);
  if (tail_reader.bad || entries >= TAIL_ENTRY_CEILING) return;
  /* Bound the product and both additions before computing a candidate. */
  if (entries > (SIZE_MAX - TAIL_PADDED_PREFIX_SIZE - TAIL_RESERVED_SIZE) /
                    TAIL_WIDE_STRIDE)
    return;
  size_t skips[] = {TAIL_PREFIX_SIZE + entries * TAIL_WIDE_STRIDE,
                    TAIL_PADDED_PREFIX_SIZE + entries * TAIL_NARROW_STRIDE,
                    TAIL_PREFIX_SIZE + entries * TAIL_NARROW_STRIDE};
  for (size_t field_index = 0; sn_work(1), (field_index < 3); field_index++) {
    sn_reader reader = tail_reader;
    reader.pos = start;
    (void)sn_take(&reader, skips[field_index] + TAIL_RESERVED_SIZE);
    size_t more = sn_count(&reader, CONTROL_STRIDE);
    (void)sn_take(&reader, more * CONTROL_STRIDE);
    uint32_t width = sn_u32(&reader), height = sn_u32(&reader);
    if (reader.bad || !is_known_sheet(width, height)) continue;
    uint32_t style = sn_u32(&reader);
    if (reader.bad) continue;
    stroke->has_style = true;
    stroke->style = style;
    (void)sn_take(&reader, ATTRIBUTE_FLAG_SIZE);
    size_t len = sn_count(&reader, 1);
    sn_bytes bytes = sn_take(&reader, len);
    if (reader.bad || !len ||
        (len == 4 && !sn_work_memcmp(bytes.data, "none", 4)))
      return;
    size_t count = 1;
    for (size_t index = 0; sn_work(1), (index < len); index++)
      if (bytes.data[index] == ',') count++;
    sn_string* fields = sn_alloc(arena, count, sizeof *fields);
    if (!fields) return;
    size_t start_field = 0;
    for (size_t index = 0; sn_work(1), (index <= len); index++)
      if (index == len || bytes.data[index] == ',') {
        sn_bytes raw = {bytes.data + start_field, index - start_field}, decoded;
        if (sn_base64(arena, raw, false, &decoded)) raw = decoded;
        fields[stroke->attribute_count++] = sn_text(arena, raw.data, raw.size);
        start_field = index + 1;
      }
    stroke->attributes = fields;
    return;
  }
}

/* Read finite pixel-coordinate rings. A damaged outline fails independently of
 * its already decoded centreline. */
static bool read_contours(sn_arena* arena, sn_reader* reader, sn_stroke* stroke,
                          bool* impossible) {
  size_t count = sn_count(reader, COUNT_STRIDE);
  if (reader->bad) return false;
  if (!sn_declared_fits(arena, count, sizeof(sn_contour))) {
    *impossible = true;
    return false;
  }
  sn_contour* rings = sn_alloc(arena, count, sizeof *rings);
  if (!rings) return false;
  for (size_t index = 0; sn_work(1), (index < count); index++) {
    size_t point_count = sn_count(reader, POINT_STRIDE);
    if (reader->bad) return false;
    if (!sn_declared_fits(arena, point_count, sizeof(sn_point))) {
      *impossible = true;
      return false;
    }
    sn_point* points = sn_alloc(arena, point_count, sizeof *points);
    if (!points) return false;
    for (size_t field_index = 0; sn_work(1), (field_index < point_count);
         field_index++) {
      double x = sn_f32(reader), y = sn_f32(reader);
      if (!isfinite(x) || !isfinite(y)) return false;
      points[field_index] = (sn_point){x, y};
    }
    rings[index] = (sn_contour){points, point_count};
  }
  stroke->contours = rings;
  stroke->contour_count = count;
  return true;
}

/* Read the checked fixed header and bounded arrays. Preserve the centreline on
 * outline failure and avoid guessing a tail after that failure. */
static bool parse_stroke(sn_arena* arena, sn_bytes bytes, sn_size size,
                         sn_stroke* stroke) {
  if (bytes.size < STROKE_HEADER_SIZE) return false;
  sn_reader reader = {bytes.data, bytes.size, STROKE_HEADER_SIZE, false};
  stroke->pen = (int32_t)header_u32(bytes, STROKE_PEN_OFFSET);
  stroke->color = header_u32(bytes, STROKE_COLOR_OFFSET);
  stroke->thickness = header_u32(bytes, STROKE_THICKNESS_OFFSET);
  stroke->page_number = header_u32(bytes, STROKE_PAGE_OFFSET);
  stroke->role_raw = (int32_t)header_u32(bytes, STROKE_ROLE_OFFSET);
  stroke->layer = header_u32(bytes, STROKE_LAYER_OFFSET);
  sn_bytes kind = {bytes.data + STROKE_KIND_OFFSET, STROKE_KIND_SIZE};
  size_t kind_length = 0;
  while (sn_work(1), (kind_length < kind.size && kind.data[kind_length]))
    kind_length++;
  stroke->kind_name = sn_text(arena, kind.data, kind_length);
  double x = (int32_t)header_u32(bytes, STROKE_LEFT_OFFSET);
  double y = (int32_t)header_u32(bytes, STROKE_TOP_OFFSET);
  double right = (int32_t)header_u32(bytes, STROKE_RIGHT_OFFSET);
  double bottom = (int32_t)header_u32(bytes, STROKE_BOTTOM_OFFSET);
  uint32_t range_y = header_u32(bytes, STROKE_RANGE_Y_OFFSET);
  uint32_t range_x = header_u32(bytes, STROKE_RANGE_X_OFFSET);
  double scale_y = range_y > 0 ? (double)range_y / size.height : 1;
  double scale_x = range_x > 0 ? (double)range_x / size.width : scale_y;
  size_t disabled = sn_count(&reader, DISABLED_STRIDE);
  (void)sn_take(&reader, disabled * DISABLED_STRIDE);
  size_t count = sn_count(&reader, POINT_STRIDE);
  if (reader.bad) return false;
  if (!sn_declared_fits(arena, count, sizeof(sn_stroke_point))) return false;
  sn_stroke_point* points = sn_alloc(arena, count, sizeof *points);
  if (!points) return false;
  for (size_t index = 0; sn_work(1), (index < count); index++) {
    double raw_y = sn_u32(&reader), raw_x = sn_u32(&reader);
    points[index].position =
        (sn_point){size.width - raw_x / scale_x, raw_y / scale_y};
  }
  stroke->points = points;
  stroke->point_count = count;
  size_t pressures = sn_count(&reader, PRESSURE_STRIDE);
  if (reader.bad) return false;
  if (!sn_declared_fits(arena, pressures, sizeof(uint16_t))) return false;
  uint16_t* pressure = sn_alloc(arena, pressures, sizeof *pressure);
  if (!pressure) return false;
  for (size_t index = 0; sn_work(1), (index < pressures); index++) {
    pressure[index] = sn_u16(&reader);
    if (index < count) points[index].pressure = pressure[index];
  }
  stroke->pressures = pressure;
  stroke->pressure_count = pressures;
  if (pressures != count)
    sn_log_writef(SN_LOG_DEBUG, "stroke point and pressure counts differ");
  for (size_t index = 0;
       sn_work(1), (index < sizeof SKIPPED_ARRAY_STRIDES /
                                sizeof *SKIPPED_ARRAY_STRIDES);
       index++) {
    size_t skipped_count = sn_count(&reader, SKIPPED_ARRAY_STRIDES[index]);
    (void)sn_take(&reader, skipped_count * SKIPPED_ARRAY_STRIDES[index]);
  }
  stroke->status_raw = (int32_t)sn_u32(&reader);
  stroke->operation = (int32_t)sn_u32(&reader);
  stroke->id = (int32_t)sn_u32(&reader);
  (void)sn_take(&reader, STATUS_RESERVED_SIZE);
  size_t controls = sn_count(&reader, CONTROL_STRIDE);
  (void)sn_take(&reader, controls * CONTROL_STRIDE);
  (void)sn_take(&reader, CONTOUR_PREFIX_SIZE);
  if (reader.bad) return false;
  bool impossible = false;
  if (read_contours(arena, &reader, stroke, &impossible))
    read_attributes(arena, reader, stroke);
  else
    sn_log_writef(SN_LOG_DEBUG, "stroke outline does not read");
  if (impossible) return false;
  sn_classify(stroke);
  if (count && (stroke->kind == SN_STROKE_KIND_FRAME ||
                stroke->kind == SN_STROKE_KIND_HEADING_BOX)) {
    x = right = points[0].position.x;
    y = bottom = points[0].position.y;
    for (size_t index = 1; sn_work(1), (index < count); index++) {
      x = fmin(x, points[index].position.x);
      right = fmax(right, points[index].position.x);
      y = fmin(y, points[index].position.y);
      bottom = fmax(bottom, points[index].position.y);
    }
  }
  stroke->bounds = (sn_rect){fmin(x, right), fmin(y, bottom), fabs(right - x),
                             fabs(bottom - y), false};
  return !arena->failed;
}

/* Decode an optional compressed envelope, then isolate records by length. A
 * truncated log counts the remaining records as skipped. */
static void strokes_read(sn_arena* arena, sn_arena* scratch, sn_bytes bytes,
                         sn_page* page) {
  sn_reader reader = {bytes.data, bytes.size, 0, false};
  uint32_t first = sn_u32(&reader);
  size_t count = first;
  if (first == UINT32_MAX) {
    count = sn_u32(&reader);
    size_t expanded = sn_u32(&reader), packed = sn_u32(&reader);
    sn_bytes input = sn_take(&reader, packed), payload;
    if (reader.bad || !sn_lz4(scratch, input, expanded, &payload) ||
        count > payload.size / 4)
      goto fail;
    reader = (sn_reader){payload.data, payload.size, 0, false};
  } else if (reader.bad || count > (reader.size - reader.pos) / 4)
    goto fail;
  sn_stroke* strokes = nullptr;
  size_t capacity = 0;
  for (size_t index = 0; sn_work(1), (index < count); index++) {
    size_t length = sn_count(&reader, 1);
    sn_bytes record_bytes = sn_take(&reader, length);
    if (reader.bad) {
      page->skipped_strokes += count - index;
      sn_log_writef(SN_LOG_ERROR, "stroke log ends inside a record");
      break;
    }
    sn_stroke stroke = {0};
    if (parse_stroke(arena, record_bytes, page->size, &stroke)) {
      if (page->stroke_count == capacity) {
        size_t next = capacity ? capacity * 2 : 8;
        sn_stroke* grown = sn_alloc(arena, next, sizeof *grown);
        if (!grown) return;
        if (capacity)
          sn_work_memcpy(grown, strokes, page->stroke_count * sizeof *grown);
        strokes = grown;
        capacity = next;
      }
      strokes[page->stroke_count++] = stroke;
    } else {
      page->skipped_strokes++;
      sn_log_writef(SN_LOG_ERROR, "stroke record does not read");
    }
  }
  if (reader.pos < reader.size)
    sn_log_writef(SN_LOG_DEBUG, "stroke log has trailing bytes");
  page->strokes = strokes;
  return;
fail:
  page->skipped_strokes = 1;
  sn_log_writef(SN_LOG_ERROR, "stroke log does not read");
}

/* Return a non-sentinel tail field or absence when the field index is missing.
 */
static sn_string stroke_attribute(const sn_stroke* stroke, size_t index) {
  return index < stroke->attribute_count ? sn_unset(stroke->attributes[index])
                                         : (sn_string){0};
}

/* Build drawn-stroke and text-box views without copying stroke geometry; layer
 * visibility filters only the drawn view. */
void sn_derived(sn_arena* arena, sn_page* page) {
  const sn_stroke** drawn = sn_alloc(arena, page->stroke_count, sizeof *drawn);
  sn_text_box* boxes = sn_alloc(arena, page->stroke_count, sizeof *boxes);
  if (!drawn || !boxes) return;
  for (size_t index = 0; sn_work(1), (index < page->stroke_count); index++) {
    const sn_stroke* stroke = page->strokes + index;
    bool visible = true;
    for (size_t entry_index = 0; sn_work(1), (entry_index < page->layer_count);
         entry_index++) {
      int64_t id = sn_layer_name_identifier(page->layers[entry_index].name);
      if (id == stroke->layer && !page->layers[entry_index].is_visible)
        visible = false;
    }
    if (stroke->is_drawn && visible) drawn[page->drawn_stroke_count++] = stroke;
    if (stroke->category == SN_CATEGORY_TEXT_BOX && stroke->status_raw >= 0) {
      sn_text_box* box = boxes + page->text_box_count++;
      box->id = stroke->id;
      box->rect = stroke->bounds;
      (void)sn_rect_parse(stroke_attribute(stroke, 4), &box->rect);
      box->font_path = stroke_attribute(stroke, 11);
      box->timestamp = stroke_attribute(stroke, 3);
      box->text = stroke->attribute_count > 12 &&
                          !sn_equal(stroke->attributes[12], "none")
                      ? stroke->attributes[12]
                      : sn_text(arena, (const uint8_t*)"", 0);
      sn_string font = stroke_attribute(stroke, 10);
      box->has_font_size =
          sn_double(font, &box->font_size) && isfinite(box->font_size);
      if (!box->has_font_size) box->font_size = 0;
      box->attributes = stroke->attributes;
      box->attribute_count = stroke->attribute_count;
    }
  }
  page->drawn_strokes = drawn;
  page->text_boxes = boxes;
}

typedef struct cached_strokes {
  const sn_stroke* strokes;
  size_t stroke_count, skipped_strokes;
} cached_strokes;

void sn_strokes(sn_arena* arena, sn_bytes bytes, sn_page* page) {
  /* Standalone decoder inputs have no file offset and are deliberately not
   * cached. */
  if (!arena->file_base) {
    sn_arena scratch = {.budget = arena->budget};
    strokes_read(arena, &scratch, bytes, page);
    sn_scratch_free(arena, &scratch);
    return;
  }
  size_t context = 0;
  for (size_t index = 0; sn_work(1), (index < SN_SHEET_COUNT); index++) {
    if (page->size.width == sn_sheets[index].width &&
        page->size.height == sn_sheets[index].height)
      context = 1 + index * 2;
    if (page->size.width == sn_sheets[index].height &&
        page->size.height == sn_sheets[index].width)
      context = 2 + index * 2;
  }
  cached_strokes* cached =
      sn_cached(arena, sn_offset(arena, bytes.data), SN_CACHE_STROKES, context);
  if (cached) {
    page->strokes = cached->strokes;
    page->stroke_count = cached->stroke_count;
    page->skipped_strokes = cached->skipped_strokes;
    return;
  }
  sn_decode_begin(arena, sn_offset(arena, bytes.data), SN_CACHE_STROKES,
                  context);
  sn_arena scratch = {.budget = arena->budget};
  strokes_read(arena, &scratch, bytes, page);
  sn_scratch_free(arena, &scratch);
  cached_strokes result = {page->strokes, page->stroke_count,
                           page->skipped_strokes};
  sn_cache_put(arena, sn_offset(arena, bytes.data), SN_CACHE_STROKES, context,
               &result, sizeof result);
}
