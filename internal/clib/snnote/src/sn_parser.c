/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
constexpr size_t FILE_KIND_SIZE = 4;
constexpr size_t FILE_SIGNATURE_SIZE = 16;
constexpr size_t FILE_MARKER_SIZE = 12;
constexpr size_t FILE_VERSION_SIZE = 8;
constexpr size_t FILE_HEADER_SIZE = 24;
constexpr size_t FILE_ADDRESS_SIZE = 4;
constexpr int LAYER_COUNT = 5;
constexpr int64_t ORIENTATION_PORTRAIT_FIRST = 1000;
constexpr int64_t ORIENTATION_PORTRAIT_SECOND = 1180;
constexpr int64_t ORIENTATION_LANDSCAPE_FIRST = 1090;
constexpr int64_t ORIENTATION_LANDSCAPE_SECOND = 1270;

typedef struct owner {
  sn_notebook notebook;
  sn_arena arena;
  sn_budget budget;
} owner;
typedef struct cached_page {
  size_t index;
  bool valid;
} cached_page;
typedef struct page_key {
  int64_t number, address;
  size_t order;
} page_key;
/* Sort by numeric page then file order, making last-occurrence replacement
 * deterministic. */
static int compare_page_keys(const void* x, const void* y) {
  sn_work(1);
  const page_key* first = x;
  const page_key* second = y;
  if (first->number != second->number)
    return (first->number > second->number) - (first->number < second->number);
  return (first->order > second->order) - (first->order < second->order);
}

/* Coverage can be shared by many distinct pages or layer descriptors. */
static bool fits_cached(sn_arena* arena, sn_bytes bytes, size_t total) {
  bool* cached =
      sn_cached(arena, sn_offset(arena, bytes.data), SN_CACHE_COVERAGE, total);
  if (cached) return *cached;
  sn_decode_begin(arena, sn_offset(arena, bytes.data), SN_CACHE_COVERAGE,
                  total);
  bool result = sn_fits(bytes, total);
  sn_cache_put(arena, sn_offset(arena, bytes.data), SN_CACHE_COVERAGE, total,
               &result, sizeof result);
  return result;
}

/* Infer a portrait sheet only from ordinary RLE coverage, never the
 * blank-template fallback. */
static sn_size measure_sheet(sn_arena* arena, sn_bytes bytes) {
  for (size_t index = 0; sn_work(1), (index < SN_SHEET_COUNT); index++)
    if (fits_cached(
            arena, bytes,
            (size_t)sn_sheets[index].width * (size_t)sn_sheets[index].height))
      return sn_sheets[index];
  return (sn_size){0};
}

typedef struct cached_layer {
  sn_metadata metadata;
  const sn_bitmap* bitmap;
  bool valid;
} cached_layer;
/* Shared layer metadata is projected once, even when many pages name it. */
static bool read_layer(sn_arena* arena, sn_bytes file, int64_t address,
                       sn_metadata* metadata, const sn_bitmap** bitmap) {
  sn_bytes bytes;
  if (!sn_block(file, address, &bytes)) return false;
  cached_layer* cached =
      sn_cached(arena, sn_offset(arena, bytes.data), SN_CACHE_LAYER, 0);
  cached_layer result = {0};
  if (cached)
    result = *cached;
  else {
    sn_decode_begin(arena, sn_offset(arena, bytes.data), SN_CACHE_LAYER, 0);
    result.valid = sn_metadata_at(arena, file, address, &result.metadata);
    if (result.valid) {
      sn_metadata fields =
          sn_fields(arena, sn_offset(arena, bytes.data), result.metadata);
      result.valid = sn_bitmap_at(arena, file, fields, "LAYERBITMAP",
                                  "LAYERPROTOCOL", &result.bitmap);
    }
    sn_cache_put(arena, sn_offset(arena, bytes.data), SN_CACHE_LAYER, 0,
                 &result, sizeof result);
  }
  *metadata = result.metadata;
  *bitmap = result.bitmap;
  return result.valid;
}

/* Build one page transactionally in the arena. Required referenced blocks fail
 * the page; optional decoded content may be omitted. */
static bool parse_page(sn_arena* arena, sn_bytes file, page_key key,
                       sn_size sheet, sn_page* parsed_page) {
  if (key.address <= 0 ||
      !sn_metadata_at(arena, file, key.address, &parsed_page->metadata))
    return false;
  sn_metadata fields =
      sn_fields(arena, (size_t)key.address + 4, parsed_page->metadata);
  parsed_page->number = key.number;
  parsed_page->id = sn_unset(sn_field_value(fields, "PAGEID"));
  parsed_page->template_name = sn_unset(sn_field_value(fields, "PAGESTYLE"));
  parsed_page->orientation_raw = sn_field_number(fields, "ORIENTATION", 0);
  int64_t orientation = parsed_page->orientation_raw;
  parsed_page->orientation =
      (orientation == ORIENTATION_LANDSCAPE_FIRST ||
       orientation == ORIENTATION_LANDSCAPE_SECOND)
          ? SN_ORIENTATION_LANDSCAPE
      : (orientation == 0 || orientation == ORIENTATION_PORTRAIT_FIRST ||
         orientation == ORIENTATION_PORTRAIT_SECOND)
          ? SN_ORIENTATION_PORTRAIT
          : SN_ORIENTATION_OTHER;
  sn_layer_table(arena, sn_field_value(fields, "LAYERINFO"), parsed_page);
  sn_string sequence = sn_field_value(fields, "LAYERSEQ");
  sn_layer_name order[LAYER_COUNT] = {SN_LAYER_3, SN_LAYER_2, SN_LAYER_1,
                                      SN_LAYER_MAIN, SN_LAYER_BACKGROUND};
  bool stacked[LAYER_COUNT] = {true, true, true, true, true};
  size_t used = LAYER_COUNT;
  if (sequence.data) {
    used = 0;
    sn_work_memset(stacked, 0, sizeof stacked);
    size_t start = 0;
    for (size_t index = 0; sn_work(1), (index <= sequence.length); index++)
      if (index == sequence.length || sequence.data[index] == ',') {
        sn_string token = {sequence.data + start, index - start};
        for (int field_index = 0; sn_work(1), (field_index < LAYER_COUNT);
             field_index++)
          if (sn_equal(token, sn_layer_name_key((sn_layer_name)field_index)) &&
              !stacked[field_index]) {
            order[used++] = (sn_layer_name)field_index;
            stacked[field_index] = true;
          }
        start = index + 1;
      }
    for (int field_index = 0; sn_work(1), (field_index < LAYER_COUNT);
         field_index++)
      if (!stacked[field_index]) order[used++] = (sn_layer_name)field_index;
  }
  sn_layer* layers = sn_alloc(arena, LAYER_COUNT, sizeof *layers);
  if (!layers) return false;
  for (size_t index = 0; sn_work(1), (index < used); index++) {
    int name = order[index];
    int64_t address =
        sn_field_number(fields, sn_layer_name_key((sn_layer_name)name), 0);
    if (!address) continue;
    sn_layer* layer = layers + parsed_page->layer_count;
    layer->name = (sn_layer_name)name;
    layer->is_stacked = stacked[name];
    if (!read_layer(arena, file, address, &layer->metadata, &layer->bitmap))
      return false;
    int64_t id = sn_layer_name_identifier((sn_layer_name)name);
    for (size_t entry_index = 0;
         sn_work(1), (entry_index < parsed_page->layer_info_count);
         entry_index++)
      if (parsed_page->layer_info[entry_index].layer_id == id) {
        layer->info = parsed_page->layer_info + entry_index;
        break;
      }
    layer->is_visible =
        layer->is_stacked &&
        (!layer->info || (layer->info->is_visible && !layer->info->is_deleted));
    parsed_page->layer_count++;
  }
  parsed_page->layers = layers;
  sn_string machine = sn_unset(sn_field_value(fields, "PAGEMACHINETYPE"));
  sn_size named = sn_sheet(machine);
  if (named.width != 0) sheet = named;
  for (size_t index = 0; sn_work(1), (index < parsed_page->layer_count);
       index++)
    if (layers[index].bitmap &&
        layers[index].bitmap->encoding == SN_ENCODING_RATTA_RLE) {
      sn_bytes bytes = layers[index].bitmap->bytes;
      if (!fits_cached(arena, bytes,
                       (size_t)sheet.width * (size_t)sheet.height)) {
        sn_size measured = measure_sheet(arena, bytes);
        if (measured.width != 0) {
          sheet = measured;
          sn_log_writef(SN_LOG_DEBUG, "page sheet measured from bitmap");
        }
      }
      break;
    }
  parsed_page->size = parsed_page->orientation == SN_ORIENTATION_LANDSCAPE
                          ? (sn_size){sheet.height, sheet.width}
                          : sheet;
  sn_bytes log, recognition;
  if (!sn_block(file, sn_field_number(fields, "TOTALPATH", 0), &log))
    return false;
  if (log.size) sn_strokes(arena, log, parsed_page);
  if (!sn_block(file, sn_field_number(fields, "RECOGNTEXT", 0), &recognition))
    return false;
  if (recognition.data)
    parsed_page->recognition = sn_recognition_read(arena, recognition);
  if (!sn_block(file, sn_field_number(fields, "RECOGNFILE", 0),
                &parsed_page->recognition_archive))
    return false;
  sn_string disabled = sn_unset(sn_field_value(fields, "DISABLE"));
  if (disabled.data) {
    size_t count = 1;
    for (size_t index = 0; sn_work(1), (index < disabled.length); index++)
      if (disabled.data[index] == '|') count++;
    sn_rect* rects = sn_alloc(arena, count, sizeof *rects);
    if (!rects) return false;
    size_t start = 0;
    for (size_t index = 0; sn_work(1), (index <= disabled.length); index++)
      if (index == disabled.length || disabled.data[index] == '|') {
        if (sn_rect_parse((sn_string){disabled.data + start, index - start},
                          rects + parsed_page->disabled_area_count))
          parsed_page->disabled_area_count++;
        start = index + 1;
      }
    parsed_page->disabled_areas = rects;
  }
  sn_derived(arena, parsed_page);
  return !arena->failed;
}

/* Release the private owner and its arena; the notebook is its first member and
 * null is accepted. */
void sn_notebook_free(sn_notebook* notebook) {
  if (!notebook) return;
  owner* owner_data = (owner*)notebook;
  sn_arena_free(&owner_data->arena);
  if (sn_meter && sn_meter->owner == owner_data) sn_meter->owner = nullptr;
  free(owner_data);
}

/* Validate the container before publishing ownership. Optional corruption is
 * local recovery, but any failed allocation discards the whole notebook. */
static sn_status parse_notebook(const uint8_t* data, size_t size,
                                sn_notebook** out, sn_error* err) {
  if (!data || !out)
    return sn_error_at(err, SN_ERR_INVALID_ARGUMENT, 0,
                       "notebook data or output pointer is null");
  if (size < FILE_SIGNATURE_SIZE ||
      (sn_work_memcmp(data, "note", FILE_KIND_SIZE) &&
       sn_work_memcmp(data, "mark", FILE_KIND_SIZE)) ||
      sn_work_memcmp(data + FILE_KIND_SIZE, "SN_FILE_VER_", FILE_MARKER_SIZE))
    return sn_error_at(err, SN_ERR_UNSUPPORTED_FORMAT, 0,
                       "file signature at offset 0");
  int64_t version = 0;
  if (!sn_integer((sn_string){(const char*)data + FILE_SIGNATURE_SIZE,
                              size - FILE_SIGNATURE_SIZE < FILE_VERSION_SIZE
                                  ? size - FILE_SIGNATURE_SIZE
                                  : FILE_VERSION_SIZE},
                  &version))
    return sn_error_at(err, SN_ERR_UNSUPPORTED_FORMAT, 16,
                       "file version at offset 16 is not decimal");
  if (size < FILE_HEADER_SIZE + FILE_ADDRESS_SIZE)
    return sn_error_at(err, SN_ERR_UNEXPECTED_EOF, (int64_t)size,
                       "file ends before footer address");
  owner* own = sn_calloc(1, sizeof *own);
  if (!own)
    return sn_error_at(err, SN_ERR_OUT_OF_MEMORY, 0,
                       "notebook owner allocation");
  if (sn_meter) sn_meter->owner = own;
  own->budget.limit =
      size > (SIZE_MAX - SN_PARSE_BUDGET_FLOOR) / SN_PARSE_BUDGET_MULTIPLE
          ? SIZE_MAX
          : SN_PARSE_BUDGET_FLOOR + size * SN_PARSE_BUDGET_MULTIPLE;
  own->budget.live = sizeof *own;
  own->arena.budget = &own->budget;
  sn_arena* arena = &own->arena;
  sn_notebook* notebook = &own->notebook;
  sn_status failure = SN_ERR_MALFORMED;
  const char* failure_detail = "file copy allocation";
  int64_t failure_value = 0;
  uint8_t* copy = sn_alloc(arena, size, 1);
  if (!copy) goto fail;
  sn_work_memcpy(copy, data, size);
  sn_bytes file = {copy, size};
  arena->file_base = copy;
  arena->file_size = size;
  notebook->kind = data[0] == 'n' ? SN_KIND_NOTE : SN_KIND_MARK;
  notebook->format_version = version;
  sn_reader tail = {copy, size, size - FILE_ADDRESS_SIZE, false};
  size_t footer = sn_u32(&tail);
  failure_detail = "footer block address or metadata";
  failure_value = (int64_t)footer;
  if (footer < FILE_HEADER_SIZE || footer >= size - FILE_ADDRESS_SIZE)
    goto fail;
  failure = sn_block_status(file, (int64_t)footer);
  if (failure != SN_OK) goto fail;
  failure = SN_ERR_MALFORMED;
  if (!sn_metadata_at(arena, file, (int64_t)footer, &notebook->footer) ||
      !notebook->footer.count)
    goto fail;
  int64_t header =
      sn_number(notebook->footer, "FILE_FEATURE", FILE_HEADER_SIZE);
  failure_detail = "header block FILE_FEATURE address or metadata";
  failure_value = header;
  failure = sn_block_status(file, header);
  if (failure != SN_OK) goto fail;
  failure = SN_ERR_MALFORMED;
  if (!sn_metadata_at(arena, file, header, &notebook->header)) goto fail;
  sn_metadata header_fields =
      sn_fields(arena, header ? (size_t)header + 4 : 0, notebook->header);
  notebook->equipment_raw = sn_field_value(header_fields, "APPLY_EQUIPMENT");
  if (!notebook->equipment_raw.data)
    notebook->equipment_raw = sn_text(arena, (const uint8_t*)"", 0);
  notebook->equipment = sn_equipment_of(notebook->equipment_raw);
  notebook->file_id = sn_unset(sn_field_value(header_fields, "FILE_ID"));
  failure_detail = "footer page-index allocation";
  failure_value = (int64_t)notebook->footer.count;
  page_key* keys = sn_alloc(arena, notebook->footer.count, sizeof *keys);
  if (!keys) goto fail;
  size_t count = 0;
  for (size_t index = 0; sn_work(1), (index < notebook->footer.count);
       index++) {
    sn_metadata_entry entry = notebook->footer.entries[index];
    int64_t number, address = 0;
    if (entry.key.length <= 4 || sn_work_memcmp(entry.key.data, "PAGE", 4) ||
        !sn_integer((sn_string){entry.key.data + 4, entry.key.length - 4},
                    &number))
      continue;
    (void)sn_integer(entry.value, &address);
    keys[count++] = (page_key){number, address, index};
  }
  sn_work_sort(keys, count, sizeof *keys, compare_page_keys);
  size_t unique = 0;
  for (size_t index = 0; sn_work(1), (index < count); index++) {
    if (index + 1 < count && keys[index].number == keys[index + 1].number) {
      sn_log_writef(SN_LOG_DEBUG, "duplicate page entry");
      continue;
    }
    keys[unique++] = keys[index];
  }
  notebook->sheet = notebook->equipment == SN_EQUIPMENT_OTHER
                        ? (sn_size){0}
                        : sn_sheet(notebook->equipment_raw);
  if (notebook->sheet.width == 0) {
    for (size_t index = 0;
         sn_work(1), (index < unique && notebook->sheet.width == 0); index++) {
      sn_bytes page_bytes;
      if (!sn_block(file, keys[index].address, &page_bytes)) continue;
      sn_size* cached_sheet = sn_cached(
          arena, sn_offset(arena, page_bytes.data), SN_CACHE_SHEET, 0);
      if (cached_sheet) {
        notebook->sheet = *cached_sheet;
        continue;
      }
      sn_decode_begin(arena, sn_offset(arena, page_bytes.data), SN_CACHE_SHEET,
                      0);
      sn_metadata metadata;
      if (!sn_metadata_at(arena, file, keys[index].address, &metadata))
        continue;
      sn_metadata fields =
          sn_fields(arena, sn_offset(arena, page_bytes.data), metadata);
      for (size_t field_index = 0; sn_work(1), (field_index < LAYER_COUNT);
           field_index++) {
        sn_metadata layer;
        const sn_bitmap* bitmap;
        if (!read_layer(
                arena, file,
                sn_field_number(
                    fields, sn_layer_name_key((sn_layer_name)field_index), 0),
                &layer, &bitmap))
          continue;
        if (bitmap && bitmap->encoding == SN_ENCODING_RATTA_RLE) {
          notebook->sheet = measure_sheet(arena, bitmap->bytes);
          if (notebook->sheet.width != 0) break;
        }
      }
      sn_cache_put(arena, sn_offset(arena, page_bytes.data), SN_CACHE_SHEET, 0,
                   &notebook->sheet, sizeof notebook->sheet);
    }
    if (notebook->sheet.width == 0) {
      failure_detail =
          "APPLY_EQUIPMENT and page bitmaps do not identify a sheet";
      failure_value = 0;
      goto fail;
    }
    sn_log_writef(SN_LOG_DEBUG, "notebook sheet measured from bitmap");
  }
  failure_detail = "page-array allocation";
  failure_value = (int64_t)unique;
  sn_page* pages = sn_alloc(arena, unique, sizeof *pages);
  if (!pages) goto fail;
  notebook->palette = sn_palette_for_format(version);
  for (size_t index = 0; sn_work(1), (index < unique); index++) {
    failure_detail = "page model allocation";
    failure_value = keys[index].number;
    sn_page parsed_page = {0};
    sn_bytes page_bytes;
    bool valid = sn_block(file, keys[index].address, &page_bytes);
    cached_page* cached =
        valid ? sn_cached(arena, sn_offset(arena, page_bytes.data),
                          SN_CACHE_PAGE, 0)
              : nullptr;
    bool parsed = cached && cached->valid;
    if (cached && cached->valid)
      parsed_page = pages[cached->index];
    else if (!cached && valid) {
      sn_decode_begin(arena, sn_offset(arena, page_bytes.data), SN_CACHE_PAGE,
                      0);
      parsed =
          parse_page(arena, file, keys[index], notebook->sheet, &parsed_page);
      cached_page result = {notebook->page_count, parsed};
      sn_cache_put(arena, sn_offset(arena, page_bytes.data), SN_CACHE_PAGE, 0,
                   &result, sizeof result);
    }
    parsed_page.number = keys[index].number;
    if (parsed)
      pages[notebook->page_count++] = parsed_page;
    else {
      notebook->skipped_pages++;
      sn_log_writef(SN_LOG_ERROR, "page does not read");
    }
    if (arena->failed) goto fail;
  }
  notebook->pages = pages;
  failure_detail = "footer annotations or HIGHLIGHTINFO allocation";
  failure_value = (int64_t)footer;
  sn_annotations(arena, file, notebook, header_fields);
  if (arena->failed) goto fail;
  *out = notebook;
  return sn_error_set(err, SN_OK, 0);
fail:
  if (arena->failed) {
    failure = own->budget.exceeded ? SN_ERR_LIMIT : SN_ERR_OUT_OF_MEMORY;
    if (own->budget.exceeded)
      failure_detail = "parse allocation budget exceeded";
  }
  sn_notebook_free(notebook);
  return sn_error_at(err, failure, failure_value, failure_detail);
}

/* Keep the meter in the caller frame: its fields remain defined after the
 * helper's setjmp/longjmp. Publish the notebook only after complete success. */
static sn_status parse_metered(sn_work_meter* meter, const uint8_t* data,
                               size_t size, sn_notebook** out, sn_error* err) {
  if (setjmp(meter->escape)) {
    sn_work_release(meter, true);
    return sn_error_at(err, SN_ERR_LIMIT, 0, "parse work budget exceeded");
  }
  sn_status status = parse_notebook(data, size, out, err);
  sn_work_release(meter, false);
  return status;
}
sn_status sn_parse_notebook(const uint8_t* data, size_t size, sn_notebook** out,
                            sn_error* err) {
  sn_work_meter meter = {
      .limit = size > (SIZE_MAX - SN_PARSE_WORK_FLOOR) / SN_PARSE_WORK_MULTIPLE
                   ? SIZE_MAX
                   : SN_PARSE_WORK_FLOOR + size * SN_PARSE_WORK_MULTIPLE};
  sn_work_meter* previous = sn_meter;
  sn_meter = &meter;
  sn_status status = parse_metered(&meter, data, size, out, err);
  sn_meter = previous;
#ifdef SNNOTE_TESTING
  sn_test_work_finish(&meter);
#endif
  return status;
}
