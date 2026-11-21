/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
constexpr size_t PALETTE_SIZE = 256;
constexpr uint8_t PALETTE_BLACK = 0x61;
constexpr uint8_t PALETTE_BACKGROUND = 0x62;
constexpr uint8_t PALETTE_DARK = 0x63;
constexpr uint8_t PALETTE_GRAY = 0x64;
constexpr uint8_t PALETTE_WHITE = 0x65;
constexpr uint8_t PALETTE_MARKER_BLACK = 0x66;
constexpr uint8_t PALETTE_MARKER_DARK = 0x67;
constexpr uint8_t PALETTE_MARKER_GRAY = 0x68;
constexpr int64_t FORMAT_SUFFIX_DIVISOR = 10000;
constexpr int64_t CURRENT_PALETTE_VERSION = 15;
constexpr size_t PNG_SIGNATURE_SIZE = 8;
constexpr size_t JPEG_SIGNATURE_SIZE = 3;
constexpr int64_t TITLE_COMPONENT_BASE = 1000;
constexpr int32_t PEN_NON_INK = -3;
constexpr int32_t ROLE_INK_CODE = 5000;
constexpr int32_t ROLE_DERIVED_CODE = 0;
constexpr int32_t ROLE_ERASER_CODE = -1;
constexpr int32_t ROLE_ERASER_SECOND_CODE = -2;
constexpr int32_t ROLE_ERASER_THIRD_CODE = -4;
constexpr int32_t ROLE_LASSO_CODE = -5;
constexpr int32_t STATUS_CODES[] = {0, -2, -3, -4, -16, -99};
constexpr uint32_t HEADING_STYLE_FIRST = 1000000;
constexpr uint32_t HEADING_STYLE_LAST = 1255255;
constexpr uint32_t TEXT_BOX_STYLE_FIRST = 5000000;
constexpr uint32_t TEXT_BOX_STYLE_LAST = 5002000;
constexpr uint32_t PICTURE_LINK_STYLE_FIRST = 24000000;
constexpr uint32_t PICTURE_LINK_STYLE_LAST = 24000003;
constexpr uint32_t TEXT_LINK_STYLE_FIRST = 25000000;
constexpr uint32_t TEXT_LINK_STYLE_LAST = 25000003;
constexpr uint32_t LINK_STYLE_FIRST = 20000000;
constexpr uint32_t LINK_STYLE_LAST = 26000000;

/* Return static status text without allocation, including a fallback for
 * unknown status values. */
const char* sn_status_string(sn_status status) {
  switch (status) {
    case SN_OK:
      return "success";
    case SN_ERR_INVALID_ARGUMENT:
      return "invalid argument";
    case SN_ERR_OUT_OF_MEMORY:
      return "out of memory";
    case SN_ERR_UNSUPPORTED_FORMAT:
      return "unsupported format";
    case SN_ERR_UNEXPECTED_EOF:
      return "unexpected end of data";
    case SN_ERR_MALFORMED:
      return "malformed data";
    case SN_ERR_IMAGE_ENCODING:
      return "PNG or JPEG requires an image decoder";
    case SN_ERR_UNSUPPORTED_ENCODING:
      return "unsupported bitmap encoding";
    case SN_ERR_LIMIT:
      return "size limit exceeded";
  }
  return "unknown status";
}

/* Build the indexed palette; unassigned byte codes cover nothing. */
sn_palette sn_palette_legacy(void) {
  sn_palette palette;
  for (size_t index = 0; sn_work(1), (index < PALETTE_SIZE); index++)
    palette.inks[index] = (sn_ink){255, 0};
  constexpr uint8_t codes[] = {PALETTE_BLACK,        PALETTE_DARK,
                               PALETTE_GRAY,         PALETTE_WHITE,
                               PALETTE_MARKER_BLACK, PALETTE_MARKER_DARK,
                               PALETTE_MARKER_GRAY};
  constexpr uint8_t grays[] = {SN_COLOR_BLACK, SN_COLOR_DARK,  SN_COLOR_GRAY,
                               SN_COLOR_WHITE, SN_COLOR_BLACK, SN_COLOR_DARK,
                               SN_COLOR_GRAY};
  for (size_t index = 0; sn_work(1), (index < sizeof codes / sizeof *codes);
       index++)
    palette.inks[codes[index]] = (sn_ink){grays[index], 255};
  return palette;
}

/* Build the swapped code/gray palette; the reserved background code remains
 * transparent. */
sn_palette sn_palette_current(void) {
  sn_palette palette;
  for (size_t index = 0; sn_work(1), (index < PALETTE_SIZE); index++)
    palette.inks[index] = (sn_ink){(uint8_t)index, 255};
  constexpr uint8_t grays[] = {SN_COLOR_BLACK,
                               SN_COLOR_ERASER,
                               SN_COLOR_DARK_LEGACY,
                               SN_COLOR_GRAY_LEGACY,
                               SN_COLOR_WHITE,
                               SN_COLOR_MARKER_BLACK,
                               SN_COLOR_MARKER_DARK_LEGACY,
                               SN_COLOR_MARKER_GRAY_LEGACY};
  for (size_t index = 0; sn_work(1), (index < sizeof grays / sizeof *grays);
       index++) {
    palette.inks[PALETTE_BLACK + index] = (sn_ink){grays[index], 255};
    palette.inks[grays[index]] =
        (sn_ink){(uint8_t)(PALETTE_BLACK + index), 255};
  }
  palette.inks[PALETTE_BACKGROUND] = (sn_ink){255, 0};
  return palette;
}

/* Select the palette from the format suffix rather than its year prefix. */
sn_palette sn_palette_for_format(int64_t version) {
  return version % FORMAT_SUFFIX_DIVISOR >= CURRENT_PALETTE_VERSION
             ? sn_palette_current()
             : sn_palette_legacy();
}

/* Recognize equipment names and aliases; preserve unknown identity for sheet
 * inference. */
sn_equipment sn_equipment_of(sn_string name) {
  if (sn_equal(name, "A5X")) return SN_EQUIPMENT_A5X;
  if (sn_equal(name, "A6X")) return SN_EQUIPMENT_A6X;
  if (sn_equal(name, "N6") || sn_equal(name, "A6X2")) return SN_EQUIPMENT_NOMAD;
  if (sn_equal(name, "N5") || sn_equal(name, "A5X2")) return SN_EQUIPMENT_MANTA;
  return SN_EQUIPMENT_OTHER;
}
/* Sheet dimensions have one authority; inference and tail probing use it too.
 */
const sn_size sn_sheets[2] = {{1404, 1872}, {1920, 2560}};

/* Return the shared portrait sheet entry, or an empty size for an unknown
 * device. */
sn_size sn_equipment_sheet(sn_equipment equipment) {
  switch (equipment) {
    case SN_EQUIPMENT_A5X:
    case SN_EQUIPMENT_A6X:
    case SN_EQUIPMENT_NOMAD:
      return sn_sheets[0];
    case SN_EQUIPMENT_MANTA:
      return sn_sheets[1];
    default:
      return (sn_size){0};
  }
}

/* Numeric machine codes precede the equipment names in older page headers. */
sn_size sn_sheet(sn_string name) {
  if (sn_equal(name, "5")) return sn_sheets[1];
  if (name.length == 1 && name.data[0] >= '0' && name.data[0] <= '4')
    return sn_sheets[0];
  return sn_equipment_sheet(sn_equipment_of(name));
}

/* Classify by image signature before trusting the protocol field; retain
 * unknown protocol names. */
sn_bitmap sn_bitmap_make(sn_bytes bytes, sn_string protocol) {
  sn_bitmap bitmap = {.bytes = bytes, .encoding = SN_ENCODING_RATTA_RLE};
  if (bytes.size >= PNG_SIGNATURE_SIZE &&
      sn_work_memcmp(bytes.data, "\x89PNG\r\n\x1a\n", PNG_SIGNATURE_SIZE) == 0)
    bitmap.encoding = SN_ENCODING_PNG;
  else if (bytes.size >= JPEG_SIGNATURE_SIZE &&
           sn_work_memcmp(bytes.data, "\xff\xd8\xff", JPEG_SIGNATURE_SIZE) == 0)
    bitmap.encoding = SN_ENCODING_JPEG;
  else if (sn_unset(protocol).data && !sn_equal(protocol, "RATTA_RLE")) {
    bitmap.encoding =
        sn_equal(protocol, "PNG") ? SN_ENCODING_PNG : SN_ENCODING_OTHER;
    if (bitmap.encoding == SN_ENCODING_OTHER) bitmap.name = protocol;
  }
  return bitmap;
}

/* Inspect each payload signature once. Protocol overrides are contextual views
 * over that shared payload and do not change its interpretation for other keys.
 */
const sn_bitmap* sn_bitmap_decode(sn_arena* arena, sn_bytes bytes,
                                  sn_string protocol) {
  if (sn_offset(arena, bytes.data) == SIZE_MAX ||
      protocol.length > SN_PROTOCOL_MAX_LENGTH) {
    sn_bitmap* value = sn_alloc(arena, 1, sizeof *value);
    if (value) {
      *value = sn_bitmap_make(bytes, protocol);
      if (protocol.length > SN_PROTOCOL_MAX_LENGTH) {
        value->encoding = SN_ENCODING_OTHER;
        value->name = protocol;
      }
    }
    return value;
  }
  sn_bitmap* base =
      sn_cached(arena, sn_offset(arena, bytes.data), SN_CACHE_BITMAP, 0);
  if (!base) {
    sn_decode_begin(arena, sn_offset(arena, bytes.data), SN_CACHE_BITMAP, 0);
    sn_bitmap value = sn_bitmap_make(bytes, (sn_string){0});
    sn_cache_put(arena, sn_offset(arena, bytes.data), SN_CACHE_BITMAP, 0,
                 &value, sizeof value);
    base = sn_cached(arena, sn_offset(arena, bytes.data), SN_CACHE_BITMAP, 0);
    if (!base) return nullptr;
  }
  if (base->encoding != SN_ENCODING_RATTA_RLE || !sn_unset(protocol).data ||
      sn_equal(protocol, "RATTA_RLE"))
    return base;
  size_t context = sn_protocol(arena, protocol);
  if (arena->failed) return nullptr;
  sn_bitmap* cached = sn_cached(arena, sn_offset(arena, bytes.data),
                                SN_CACHE_BITMAP_VIEW, context);
  if (cached) return cached;
  sn_decode_begin(arena, sn_offset(arena, bytes.data), SN_CACHE_BITMAP_VIEW,
                  context);
  sn_bitmap value = *base;
  value.encoding =
      sn_equal(protocol, "PNG") ? SN_ENCODING_PNG : SN_ENCODING_OTHER;
  if (value.encoding == SN_ENCODING_OTHER) value.name = protocol;
  sn_cache_put(arena, sn_offset(arena, bytes.data), SN_CACHE_BITMAP_VIEW,
               context, &value, sizeof value);
  cached = sn_cached(arena, sn_offset(arena, bytes.data), SN_CACHE_BITMAP_VIEW,
                     context);
  return cached;
}

/* Resolve one optional bitmap; zero address means absence. */
bool sn_bitmap_at(sn_arena* arena, sn_bytes file, sn_metadata metadata,
                  const char* key, const char* protocol,
                  const sn_bitmap** out) {
  sn_bytes bytes;
  if (!sn_block(file, sn_field_number(metadata, key, 0), &bytes)) return false;
  *out = nullptr;
  if (!bytes.data) return true;
  *out = sn_bitmap_decode(
      arena, bytes,
      protocol ? sn_field_value(metadata, protocol) : (sn_string){0});
  return !arena->failed;
}

/* Find a borrowed layer by identity, tolerating a null page. */
const sn_layer* sn_page_layer(const sn_page* page, sn_layer_name name) {
  if (page)
    for (size_t index = 0; sn_work(1), (index < page->layer_count); index++)
      if (page->layers[index].name == name) return page->layers + index;
  return nullptr;
}

/* Derive semantic kind, role, status and category while retaining raw values.
 * Drawn classification excludes editing and non-ink records. */
void sn_classify(sn_stroke* stroke) {
  const char* names[] = {"others", "straightLine", "0001", "0000",
                         "fiveStarsSignal"};
  stroke->kind = SN_STROKE_KIND_OTHER;
  for (int index = 0; sn_work(1), (index < 5); index++)
    if (sn_equal(stroke->kind_name, names[index]))
      stroke->kind = (sn_stroke_kind)index;
  int32_t role = stroke->role_raw;
  stroke->role =
      role == ROLE_INK_CODE       ? SN_ROLE_INK
      : role == ROLE_DERIVED_CODE ? SN_ROLE_DERIVED
      : (role == ROLE_ERASER_CODE || role == ROLE_ERASER_SECOND_CODE ||
         role == ROLE_ERASER_THIRD_CODE)
          ? SN_ROLE_ERASER
      : role == ROLE_LASSO_CODE ? SN_ROLE_LASSO
                                : SN_ROLE_OTHER;
  stroke->status = SN_STROKE_STATUS_OTHER;
  for (int index = 0; sn_work(1), (index < 6); index++)
    if (stroke->status_raw == STATUS_CODES[index])
      stroke->status = (sn_stroke_status)index;
  uint32_t style = stroke->style;
  stroke->category = SN_CATEGORY_ORDINARY;
  if (stroke->has_style && style) {
    stroke->category = SN_CATEGORY_OTHER;
    if (style >= HEADING_STYLE_FIRST && style <= HEADING_STYLE_LAST)
      stroke->category = SN_CATEGORY_HEADING;
    else if (style >= TEXT_BOX_STYLE_FIRST && style <= TEXT_BOX_STYLE_LAST)
      stroke->category = SN_CATEGORY_TEXT_BOX;
    else if (style >= PICTURE_LINK_STYLE_FIRST &&
             style <= PICTURE_LINK_STYLE_LAST)
      stroke->category = SN_CATEGORY_PICTURE_LINK;
    else if (style >= TEXT_LINK_STYLE_FIRST && style <= TEXT_LINK_STYLE_LAST)
      stroke->category = SN_CATEGORY_TEXT_LINK;
    else if (style >= LINK_STYLE_FIRST && style <= LINK_STYLE_LAST)
      stroke->category = SN_CATEGORY_LINK;
  }
  stroke->is_drawn =
      stroke->status_raw >= 0 && stroke->role != SN_ROLE_ERASER &&
      stroke->role != SN_ROLE_LASSO && stroke->kind != SN_STROKE_KIND_FRAME &&
      stroke->color != SN_COLOR_ERASER &&
      stroke->category != SN_CATEGORY_TEXT_BOX &&
      stroke->category != SN_CATEGORY_LINK &&
      stroke->category != SN_CATEGORY_PICTURE_LINK &&
      stroke->category != SN_CATEGORY_TEXT_LINK &&
      stroke->pen != SN_PEN_ERASER && stroke->pen != SN_PEN_LASSO &&
      stroke->pen != SN_PEN_ERASER_SELECT && stroke->pen != PEN_NON_INK;
}

/* Recognize both firmware spellings of the marker tool. */
bool sn_pen_is_marker(int32_t pen) {
  return pen == SN_PEN_MARKER || pen == SN_PEN_MARKER_LEGACY;
}

/* Normalize legacy gray IDs and marker variants; reject colors outside the
 * byte-gray space without changing output. */
bool sn_ink_color_gray(uint32_t color, uint8_t* out_gray) {
  if (!out_gray || color > UINT8_MAX) return false;
  switch (color) {
    case SN_COLOR_MARKER_BLACK:
      color = SN_COLOR_BLACK;
      break;
    case SN_COLOR_DARK_LEGACY:
    case SN_COLOR_MARKER_DARK_LEGACY:
    case SN_COLOR_MARKER_DARK:
      color = SN_COLOR_DARK;
      break;
    case SN_COLOR_GRAY_LEGACY:
    case SN_COLOR_MARKER_GRAY_LEGACY:
    case SN_COLOR_MARKER_GRAY:
      color = SN_COLOR_GRAY;
      break;
    default:
      break;
  }
  *out_gray = (uint8_t)color;
  return true;
}

/* Recognize marker color forms independently of the pen code. */
bool sn_ink_color_is_marker(uint32_t color) {
  return color == SN_COLOR_MARKER_BLACK ||
         color == SN_COLOR_MARKER_DARK_LEGACY ||
         color == SN_COLOR_MARKER_GRAY_LEGACY ||
         color == SN_COLOR_MARKER_DARK || color == SN_COLOR_MARKER_GRAY;
}

/* Extract the stored BBB component without clamping unknown styles. */
int64_t sn_title_style_background(int64_t style) {
  return (style / TITLE_COMPONENT_BASE) % TITLE_COMPONENT_BASE;
}

/* Extract the stored FFF component without clamping unknown styles. */
int64_t sn_title_style_foreground(int64_t style) {
  return style % TITLE_COMPONENT_BASE;
}

/* Identify the black-on-black title encoding used for hatching. */
bool sn_title_style_is_hatched(int64_t style) {
  return sn_title_style_background(style) == 0 &&
         sn_title_style_foreground(style) == 0;
}

/* Return a static page metadata key, or null for an invalid layer enum. */
const char* sn_layer_name_key(sn_layer_name name) {
  static const char* const keys[] = {"MAINLAYER", "LAYER1", "LAYER2", "LAYER3",
                                     "BGLAYER"};
  return name >= SN_LAYER_MAIN && name <= SN_LAYER_BACKGROUND ? keys[name]
                                                              : nullptr;
}

/* Translate the background identity to -1; invalid enum values return -2. */
int64_t sn_layer_name_identifier(sn_layer_name name) {
  if (name == SN_LAYER_BACKGROUND) return -1;
  return name >= SN_LAYER_MAIN && name <= SN_LAYER_3 ? name : -2;
}
