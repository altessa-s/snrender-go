/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef SNNOTE_TYPES_H
#define SNNOTE_TYPES_H
#include <stddef.h>
#include <stdint.h>

#include "snnote/export.h"
#ifdef __cplusplus
extern "C" {
#endif

/* All returned arrays and strings are borrowed from their owning notebook.
 * Treat the transparent structures as read-only. Text is UTF-8; malformed
 * sequences are replaced by U+FFFD. Lengths exclude the terminating NUL and
 * preserve embedded NULs. A null data pointer represents an absent string.
 * Text is compared bytewise: canonically equivalent spellings are different
 * keys. No Unicode normalization is performed.
 * No input buffer is retained. One free releases the entire notebook.
 * Recognition and highlight JSON reject malformed Unicode. Layer-table text
 * is repaired before JSON parsing. Link display text that decodes from base64
 * to invalid UTF-8 keeps its original spelling instead. */
/* ------------------------------------------------------------------ */
/* Borrowed values and geometry */
/* ------------------------------------------------------------------ */

typedef struct sn_string {
  /* Borrowed bytes; null means absent, including an absent optional string. */
  const char* data;
  /* Byte length, excluding the trailing NUL; embedded NULs are counted. */
  size_t length;
} sn_string;
typedef struct sn_bytes {
  /* Borrowed bytes; null means absent, including an absent optional string. */
  const uint8_t* data;
  /* Byte length of this view. */
  size_t size;
} sn_bytes;
typedef struct sn_point {
  /* Horizontal position, in page pixels from the left edge. */
  double x;
  /* Vertical position, in page pixels from the top edge. */
  double y;
} sn_point;
typedef struct sn_size {
  /* Horizontal extent, in pixels. */
  double width;
  /* Vertical extent, in pixels. */
  double height;
} sn_size;
/* Rectangle, disabled-area and font-size numeric text accepts ASCII decimal
 * syntax with finite results only, excluding nan, inf, infinity and hexadecimal
 * spellings. Overflowing components are ignored like other invalid components.
 */
typedef struct sn_rect {
  /* Horizontal position, in page pixels from the left edge. */
  double x;
  /* Vertical position, in page pixels from the top edge. */
  double y;
  /* Horizontal extent, in pixels. */
  double width;
  /* Vertical extent, in pixels. */
  double height;
  /* True when the format supplies no rectangle; coordinates are then ignored.
   */
  bool is_null;
} sn_rect;
/* ------------------------------------------------------------------ */
/* Operation results */
/* ------------------------------------------------------------------ */

typedef enum sn_status : int {
  /* The requested operation completed. */
  SN_OK,
  /* Null required pointer or invalid dimensions. */
  SN_ERR_INVALID_ARGUMENT,
  /* An allocation failed; no partial result is published. */
  SN_ERR_OUT_OF_MEMORY,
  /* The file signature or version field is not recognized. */
  SN_ERR_UNSUPPORTED_FORMAT,
  /* The file ends before a required field. */
  SN_ERR_UNEXPECTED_EOF,
  /* A required block or decoded extent is inconsistent. */
  SN_ERR_MALFORMED,
  /* PNG/JPEG payload requires an external image decoder. */
  SN_ERR_IMAGE_ENCODING,
  /* The bitmap declares an unknown protocol. */
  SN_ERR_UNSUPPORTED_ENCODING,
  /* Parse allocation budget or raster pixel ceiling exceeded. */
  SN_ERR_LIMIT,
} sn_status;
typedef struct sn_error {
  /* Status returned by the failed call, or SN_OK after success. */
  sn_status status;
  /* Format offset or count named by the message; zero when no numeric location
   * applies. */
  int64_t value;
  /* NUL-terminated diagnostic containing format facts, never notebook content.
   */
  char message[256];
} sn_error;
/* Static status description; unknown values return "unknown status". Never
 * null. */
SN_API const char* sn_status_string(sn_status status);
/* ------------------------------------------------------------------ */
/* Ordered metadata */
/* ------------------------------------------------------------------ */

typedef struct sn_metadata_entry {
  /* UTF-8 metadata key, compared bytewise. */
  sn_string key;
  /* Stored UTF-8 value; colons after the first delimiter are retained. */
  sn_string value;
} sn_metadata_entry;
typedef struct sn_metadata {
  /* Borrowed entries in file order; repeated keys are retained. */
  const sn_metadata_entry* entries;
  /* Number of elements in the associated array; zero permits a null array. */
  size_t count;
} sn_metadata;
/* Linear in the entry count. Return the first value for a bytewise key, or
 * null if absent. Null metadata or key is accepted and returns null.
 * The result belongs to the notebook. */
SN_API const sn_string* sn_metadata_get(const sn_metadata* metadata,
                                        const char* key);
/* Iterate repeated keys from *index, advancing past each match. Start at zero.
 * Null arguments return null; exhaustion leaves index at count. Borrowed
 * result. */
SN_API const sn_string* sn_metadata_next(const sn_metadata* metadata,
                                         const char* key, size_t* index);
/* ------------------------------------------------------------------ */
/* Encoded bitmaps and palettes */
/* ------------------------------------------------------------------ */

typedef enum sn_encoding : int {
  /* Native run-length bitmap codes. */
  SN_ENCODING_RATTA_RLE,
  /* PNG bytes, recognized by signature or protocol. */
  SN_ENCODING_PNG,
  /* JPEG bytes, recognized by signature. */
  SN_ENCODING_JPEG,
  /* Unknown stored value; inspect the retained raw field. */
  SN_ENCODING_OTHER,
} sn_encoding;
typedef struct sn_bitmap {
  /* Detected image signature takes precedence over the declared protocol. */
  sn_encoding encoding;
  /* Unknown protocol spelling; absent for recognized encodings. */
  sn_string name;
  /* Borrowed encoded payload; not decoded pixels. */
  sn_bytes bytes;
} sn_bitmap;
typedef struct sn_ink {
  /* Gray intensity, from 0 (black) to 255 (white). */
  uint8_t gray;
  /* Coverage, from 0 (transparent) to 255 (opaque). */
  uint8_t alpha;
} sn_ink;
typedef struct sn_palette {
  /* One gray/alpha pair for each possible decoded byte code. */
  sn_ink inks[256];
} sn_palette;
/* Palette before format suffix 15; unassigned codes are transparent. */
SN_API sn_palette sn_palette_legacy(void);
/* Palette for suffix 15 onward, including code/gray swaps and transparency. */
SN_API sn_palette sn_palette_current(void);
/* Select by version modulo 10000; returns an independent value. */
SN_API sn_palette sn_palette_for_format(int64_t version);
/* ------------------------------------------------------------------ */
/* Device, container and layer identities */
/* ------------------------------------------------------------------ */

typedef enum sn_equipment : int {
  /* Unknown stored value; inspect the retained raw field. */
  SN_EQUIPMENT_OTHER,
  /* A5X equipment spelling. */
  SN_EQUIPMENT_A5X,
  /* A6X equipment spelling. */
  SN_EQUIPMENT_A6X,
  /* N6 or A6X2 equipment spelling. */
  SN_EQUIPMENT_NOMAD,
  /* N5 or A5X2 equipment spelling. */
  SN_EQUIPMENT_MANTA,
} sn_equipment;
typedef enum sn_kind : int {
  /* A notebook container. */
  SN_KIND_NOTE,
  /* Annotations over an imported document. */
  SN_KIND_MARK,
} sn_kind;
typedef enum sn_orientation : int {
  /* Missing, zero, 1000 or 1180; use the portrait sheet axes. */
  SN_ORIENTATION_PORTRAIT,
  /* 1090 or 1270; swap the portrait sheet axes. */
  SN_ORIENTATION_LANDSCAPE,
  /* Unknown stored value; inspect the retained raw field. */
  SN_ORIENTATION_OTHER,
} sn_orientation;
typedef enum sn_layer_name : int {
  /* Primary ink layer, key MAINLAYER and identifier 0. */
  SN_LAYER_MAIN,
  /* Ink layer keyed LAYER1, identifier 1. */
  SN_LAYER_1,
  /* Ink layer keyed LAYER2, identifier 2. */
  SN_LAYER_2,
  /* Ink layer keyed LAYER3, identifier 3. */
  SN_LAYER_3,
  /* Template layer, key BGLAYER and identifier -1. */
  SN_LAYER_BACKGROUND,
} sn_layer_name;
typedef struct sn_layer_info {
  /* Stored layer identifier: -1 background, 0 main, 1..3 other ink layers. */
  int64_t layer_id;
  /* Stored name; always present, empty when the table gives none. */
  sn_string name;
  /* Whether the layer-table row identifies the template layer. */
  bool is_background;
  /* Whether the layer is shown; deleted or unstacked layers are hidden. */
  bool is_visible;
  /* Whether the layer-table row marks the layer deleted. */
  bool is_deleted;
  /* Whether this was the active editing layer. */
  bool is_current;
} sn_layer_info;
typedef struct sn_layer {
  /* Layer identity derived from its page metadata key. */
  sn_layer_name name;
  /* Borrowed encoded bitmap, or null when absent. */
  const sn_bitmap* bitmap;
  /* Borrowed matching layer-table row, or null when unavailable. */
  const sn_layer_info* info;
  /* Borrowed metadata in stored order, including unknown keys. */
  sn_metadata metadata;
  /* Whether LAYERSEQ includes this layer; absent sequence includes all layers.
   */
  bool is_stacked;
  /* Whether the layer is shown; deleted or unstacked layers are hidden. */
  bool is_visible;
} sn_layer;
/* ------------------------------------------------------------------ */
/* Stroke records and classifications */
/* ------------------------------------------------------------------ */

typedef enum sn_stroke_kind : int {
  /* Handwritten ink, stored as others. */
  SN_STROKE_KIND_FREEHAND,
  /* Ruled line, stored as straightLine. */
  SN_STROKE_KIND_STRAIGHT_LINE,
  /* Filled title rectangle, stored as 0001. */
  SN_STROKE_KIND_HEADING_BOX,
  /* Non-ink selection frame, stored as 0000. */
  SN_STROKE_KIND_FRAME,
  /* Star signal, stored as fiveStarsSignal. */
  SN_STROKE_KIND_STAR,
  /* Unknown stored value; inspect the retained raw field. */
  SN_STROKE_KIND_OTHER,
} sn_stroke_kind;
typedef enum sn_role : int {
  /* A directly drawn stroke (role 5000). */
  SN_ROLE_INK,
  /* A generated record (role zero). */
  SN_ROLE_DERIVED,
  /* Erasing operation, raw role -1, -2 or -4. */
  SN_ROLE_ERASER,
  /* Selection operation, raw role -5. */
  SN_ROLE_LASSO,
  /* Unknown stored value; inspect the retained raw field. */
  SN_ROLE_OTHER,
} sn_role;
typedef enum sn_stroke_status : int {
  /* Unremoved record, raw status 0. */
  SN_STROKE_STATUS_LIVE,
  /* Cleared record, raw status -2. */
  SN_STROKE_STATUS_CLEARED,
  /* Moved source record, raw status -3. */
  SN_STROKE_STATUS_MOVED,
  /* Source of an eraser cut, raw status -4. */
  SN_STROKE_STATUS_PARTIALLY_ERASED,
  /* Deleted record, raw status -16. */
  SN_STROKE_STATUS_DELETED,
  /* Erased record, raw status -99. */
  SN_STROKE_STATUS_ERASED,
  /* Unknown stored value; inspect the retained raw field. */
  SN_STROKE_STATUS_OTHER,
} sn_stroke_status;
typedef enum sn_category : int {
  /* No style or zero style. */
  SN_CATEGORY_ORDINARY,
  /* Title styles 1000000 through 1255255. */
  SN_CATEGORY_HEADING,
  /* Text-box styles 5000000 through 5002000. */
  SN_CATEGORY_TEXT_BOX,
  /* Other link styles within 20000000 through 26000000. */
  SN_CATEGORY_LINK,
  /* Picture links, styles 24000000 through 24000003. */
  SN_CATEGORY_PICTURE_LINK,
  /* Text links, styles 25000000 through 25000003. */
  SN_CATEGORY_TEXT_LINK,
  /* Unknown stored value; inspect the retained raw field. */
  SN_CATEGORY_OTHER,
} sn_category;
typedef struct sn_stroke_point {
  /* Point in page pixels, x rightward and y downward from the top left. */
  sn_point position;
  /* Raw digitizer pressure; zero when the pressure array has no matching entry.
   */
  uint16_t pressure;
} sn_stroke_point;
typedef struct sn_contour {
  /* Borrowed points, in page pixel coordinates. */
  const sn_point* points;
  /* Number of elements in the associated array; zero permits a null array. */
  size_t count;
} sn_contour;
typedef struct sn_stroke {
  /* Stored record identifier. */
  int32_t id;
  /* Raw pen code; known values are SN_PEN_* constants. */
  int32_t pen;
  /* Stored role code, retained even when unknown. */
  int32_t role_raw;
  /* Stored status code, retained even when unknown. */
  int32_t status_raw;
  /* Stored operation code; no interpretation is imposed. */
  int32_t operation;
  /* Raw ink color code; sn_ink_color_gray maps known grayscale values. */
  uint32_t color;
  /* Stored pen thickness, in hundredths of a page pixel. */
  uint32_t thickness;
  /* Stored ink-layer identifier, 0 main and 1..3 other layers. */
  uint32_t layer;
  /* Stored one-based page number; zero when absent or undecodable. */
  uint32_t page_number;
  /* Stored stroke-kind spelling, including unknown names. */
  sn_string kind_name;
  /* Interpreted record kind; raw spelling/code is retained separately where
   * available. */
  sn_stroke_kind kind;
  /* Interpreted role; unknown codes are SN_ROLE_OTHER. */
  sn_role role;
  /* Interpreted stroke status; raw code remains in status_raw. */
  sn_stroke_status status;
  /* Category derived from the optional style range. */
  sn_category category;
  /* Whether this record represents visible ink before page-layer visibility is
   * applied. */
  bool is_drawn;
  /* Whether a valid tail layout supplied style; otherwise style is ignored. */
  bool has_style;
  /* Stored style code; no clamping of unknown values. */
  uint32_t style;
  /* Borrowed points, in page pixel coordinates. */
  const sn_stroke_point* points;
  /* Number of entries in point. */
  size_t point_count;
  /* Borrowed complete raw pressure array, even when its length differs from
   * points. */
  const uint16_t* pressures;
  /* Number of entries in pressure. */
  size_t pressure_count;
  /* Borrowed outline rings in page pixels; empty when the outline cannot be
   * read. */
  const sn_contour* contours;
  /* Number of entries in contour. */
  size_t contour_count;
  /* Bounds in page pixels; frame and heading bounds are derived from points. */
  sn_rect bounds;
  /* Borrowed decoded tail fields, in stored order; unknown positions are
   * retained. */
  const sn_string* attributes;
  /* Number of entries in attribute. */
  size_t attribute_count;
} sn_stroke;
typedef struct sn_text_box {
  /* Stored record identifier. */
  int32_t id;
  /* Rectangle in page pixels from the top-left corner. */
  sn_rect rect;
  /* Borrowed UTF-8 content; always present, empty when the box holds none. */
  sn_string text;
  /* Stored font reference, or absent; the library never opens it. */
  sn_string font_path;
  /* Stored timestamp spelling, or absent; no timezone conversion is applied. */
  sn_string timestamp;
  /* Whether font_size parsed as a finite decimal number. */
  bool has_font_size;
  /* Stored text-box font size; meaningful only when has_font_size is true. */
  double font_size;
  /* Borrowed decoded tail fields, in stored order; unknown positions are
   * retained. */
  const sn_string* attributes;
  /* Number of entries in attribute. */
  size_t attribute_count;
} sn_text_box;
/* ------------------------------------------------------------------ */
/* Recognition */
/* ------------------------------------------------------------------ */

typedef struct sn_word {
  /* Borrowed recognized text; always present, possibly empty. */
  sn_string label;
  /* Recognition bounds in physical millimetres from the top left; is_null when
   * no box was supplied. */
  sn_rect box;
} sn_word;
typedef struct sn_element {
  /* Stored recognition type spelling, empty when absent. */
  sn_string kind;
  /* Borrowed recognized text; absence and empty text are distinct. */
  sn_string label;
  /* Borrowed recognized words in document order. */
  const sn_word* words;
  /* Number of entries in word. */
  size_t word_count;
} sn_element;
typedef struct sn_recognition {
  /* Borrowed recognition elements in document order, including non-text
   * elements. */
  const sn_element* elements;
  /* Number of entries in element. */
  size_t element_count;
  /* Borrowed text of the whole result; always present, possibly empty. */
  sn_string text;
} sn_recognition;
/* ------------------------------------------------------------------ */
/* Pages */
/* ------------------------------------------------------------------ */

typedef struct sn_page {
  /* One-based numeric page key from the footer. */
  int64_t number;
  /* Stored PAGEID, or absent. */
  sn_string id;
  /* Stored PAGESTYLE name, or absent. */
  sn_string template_name;
  /* Interpreted page orientation; missing or zero is portrait. */
  sn_orientation orientation;
  /* Stored orientation code, or zero when missing. */
  int64_t orientation_raw;
  /* Oriented page dimensions in pixels; landscape swaps the portrait sheet
   * axes. */
  sn_size size;
  /* Borrowed layers, in front-to-back stack order; unstacked layers follow. */
  const sn_layer* layers;
  /* Number of entries in layer. */
  size_t layer_count;
  /* Borrowed layer-table rows in stored order. */
  const sn_layer_info* layer_info;
  /* Number of entries in layer_info. */
  size_t layer_info_count;
  /* Borrowed readable stroke records, including non-drawn and deleted records.
   */
  const sn_stroke* strokes;
  /* Number of entries in stroke. */
  size_t stroke_count;
  /* Number of unreadable records; one when the complete log cannot be decoded.
   */
  size_t skipped_strokes;
  /* Borrowed pointers into strokes, filtered by classification and layer
   * visibility. */
  const sn_stroke* const* drawn_strokes;
  /* Number of entries in drawn_stroke. */
  size_t drawn_stroke_count;
  /* Borrowed live text boxes derived from stroke tails. */
  const sn_text_box* text_boxes;
  /* Number of entries in text_box. */
  size_t text_box_count;
  /* Borrowed recognition model, or null when absent or unreadable. */
  const sn_recognition* recognition;
  /* Borrowed opaque recognition archive; no interpretation is imposed. */
  sn_bytes recognition_archive;
  /* Borrowed text-box exclusion rectangles in page pixels. */
  const sn_rect* disabled_areas;
  /* Number of entries in disabled_area. */
  size_t disabled_area_count;
  /* Borrowed metadata in stored order, including unknown keys. */
  sn_metadata metadata;

} sn_page;
/* Borrowed matching layer, or null if absent. A null page is accepted. */
SN_API const sn_layer* sn_page_layer(const sn_page* page, sn_layer_name name);
/* ------------------------------------------------------------------ */
/* Footer annotations */
/* ------------------------------------------------------------------ */

typedef struct sn_title {
  /* Stored one-based page number; zero when absent or undecodable. */
  int64_t page_number;
  /* Stored title level; defaults to one. */
  int64_t level;
  /* Stored style code; no clamping of unknown values. */
  int64_t style;
  /* Rectangle in page pixels from the top-left corner. */
  sn_rect rect;
  /* Stored original rectangle in page pixels; falls back to rect when absent.
   */
  sn_rect original_rect;
  /* Borrowed encoded bitmap, or null when absent. */
  const sn_bitmap* bitmap;
  /* Borrowed metadata in stored order, including unknown keys. */
  sn_metadata metadata;
} sn_title;
typedef struct sn_keyword {
  /* Stored one-based page number; zero when absent or undecodable. */
  int64_t page_number;
  /* Rectangle in page pixels from the top-left corner. */
  sn_rect rect;
  /* Stored original rectangle in page pixels; falls back to rect when absent.
   */
  sn_rect original_rect;
  /* Borrowed UTF-8 content; always present, possibly empty. */
  sn_string text;
  /* Borrowed metadata in stored order, including unknown keys. */
  sn_metadata metadata;
} sn_keyword;
typedef struct sn_link {
  /* Stored one-based page number; zero when absent or undecodable. */
  int64_t page_number;
  /* Raw LINKTYPE value: 4 identifies a web link. */
  int64_t kind;
  /* Raw LINKINOUT value; zero when absent. */
  int64_t direction;
  /* Stored target page number; meaningful only with has_target_page_number. */
  int64_t target_page_number;
  /* Stored style code; no clamping of unknown values. */
  int64_t style;
  /* True when the stored target page number is positive. */
  bool has_target_page_number;
  /* Rectangle in page pixels from the top-left corner. */
  sn_rect rect;
  /* Decoded stored link target, or absent; never opened by this library. */
  sn_string target_path;
  /* Stored target file identifier, or absent. */
  sn_string target_file_id;
  /* Stored target page identifier, or absent. */
  sn_string target_page_id;
  /* Stored timestamp spelling, or absent; no timezone conversion is applied. */
  sn_string timestamp;
  /* Borrowed UTF-8 content; a null pointer means absent, distinct from empty.
   */
  sn_string text;
  /* Alias of target_path for web links (kind 4), absent otherwise. */
  sn_string url;
  /* Borrowed encoded bitmap, or null when absent. */
  const sn_bitmap* bitmap;
  /* Borrowed metadata in stored order, including unknown keys. */
  sn_metadata metadata;
} sn_link;
typedef struct sn_highlight {
  /* Stored one-based page number; zero when absent or undecodable. */
  int64_t page_number;
  /* Raw annotation type; zero when absent. */
  int64_t kind;
  /* Raw highlight color code; zero when absent. */
  int64_t color_type;
  /* Borrowed comment; absent for a missing or empty comment. */
  sn_string comment;
  /* Borrowed screen rectangles in page pixels. */
  const sn_rect* rects;
  /* Number of entries in rect. */
  size_t rect_count;
  /* Borrowed rectangles in the imported document coordinate space. */
  const sn_rect* document_rects;
  /* Number of entries in document_rect. */
  size_t document_rect_count;
  /* Whether date was supplied as a numeric millisecond timestamp. */
  bool has_date;
  /* Seconds since Unix epoch, converted from the stored milliseconds. */
  double date;
} sn_highlight;
typedef struct sn_template {
  /* Stored name; always present. */
  sn_string name;
  /* Encoded bitmap owned by the containing notebook. */
  sn_bitmap bitmap;
} sn_template;
/* ------------------------------------------------------------------ */
/* Notebook ownership */
/* ------------------------------------------------------------------ */

typedef struct sn_notebook {
  /* Shared code-to-gray mapping for every page bitmap, chosen by format
   * version. */
  sn_palette palette;
  /* Container signature: ordinary note or document annotation mark. */
  sn_kind kind;
  /* Decimal SN_FILE_VER number, retained without truncating its year prefix. */
  int64_t format_version;
  /* Recognized device identity, or SN_EQUIPMENT_OTHER. */
  sn_equipment equipment;
  /* Stored APPLY_EQUIPMENT spelling, empty when missing. */
  sn_string equipment_raw;
  /* Stored FILE_ID, or absent. */
  sn_string file_id;
  /* Portrait sheet dimensions in pixels; each page may select a different
   * sheet. */
  sn_size sheet;
  /* Borrowed readable pages, sorted by numeric page key; last duplicate wins.
   */
  const sn_page* pages;
  /* Number of entries in page. */
  size_t page_count;
  /* Number of indexed pages that could not be read. */
  size_t skipped_pages;
  /* Borrowed titles; duplicates replace by footer key and rectangle. */
  const sn_title* titles;
  /* Number of entries in title. */
  size_t title_count;
  /* Borrowed keywords; duplicates replace by footer key and rectangle. */
  const sn_keyword* keywords;
  /* Number of entries in keyword. */
  size_t keyword_count;
  /* Borrowed links, in footer order. */
  const sn_link* links;
  /* Number of entries in link. */
  size_t link_count;
  /* Borrowed document highlights, sorted by numeric page key. */
  const sn_highlight* highlights;
  /* Number of entries in highlight. */
  size_t highlight_count;
  /* Borrowed templates; last bitmap wins while first insertion position
   * remains. */
  const sn_template* templates;
  /* Number of entries in template. */
  size_t template_count;
  /* Borrowed encoded cover, or null when absent. */
  const sn_bitmap* cover;
  /* Numeric suffix of the selected COVER_ footer key, or zero. */
  int64_t cover_type;
  /* Borrowed FILE_FEATURE metadata. */
  sn_metadata header;
  /* Borrowed footer metadata. */
  sn_metadata footer;
} sn_notebook;
/* ------------------------------------------------------------------ */
/* Derived format values */
/* ------------------------------------------------------------------ */

/* Known tool codes. Unknown values remain available in sn_stroke.pen. */
constexpr int32_t SN_PEN_INK_LEGACY = 1;
constexpr int32_t SN_PEN_ERASER = 3;
constexpr int32_t SN_PEN_LASSO = 4;
constexpr int32_t SN_PEN_MARKER_LEGACY = 5;
constexpr int32_t SN_PEN_ERASER_SELECT = 9;
constexpr int32_t SN_PEN_NEEDLE_POINT = 10;
constexpr int32_t SN_PEN_MARKER = 11;
constexpr int32_t SN_PEN_CALLIGRAPHY = 15;
constexpr int32_t SN_PEN_INK = 16;

/* Stored ink codes. Legacy gray IDs map to the same displayed gray as
 * their newer intensity spellings; marker forms retain a separate identity. */
constexpr uint32_t SN_COLOR_BLACK = 0;
constexpr uint32_t SN_COLOR_MARKER_BLACK = 1;
constexpr uint32_t SN_COLOR_DARK_LEGACY = 48;
constexpr uint32_t SN_COLOR_MARKER_DARK_LEGACY = 49;
constexpr uint32_t SN_COLOR_GRAY_LEGACY = 80;
constexpr uint32_t SN_COLOR_MARKER_GRAY_LEGACY = 81;
constexpr uint32_t SN_COLOR_DARK = 157;
constexpr uint32_t SN_COLOR_MARKER_DARK = 158;
constexpr uint32_t SN_COLOR_GRAY = 201;
constexpr uint32_t SN_COLOR_MARKER_GRAY = 202;
constexpr uint32_t SN_COLOR_WHITE = 254;
constexpr uint32_t SN_COLOR_ERASER = 255;

/* True for either marker code; unknown codes return false. */
SN_API bool sn_pen_is_marker(int32_t pen);
/* Map a stored ink code to gray (0 black, 255 white). False for codes above
 * 255 or null out_gray; on false the output is unchanged. */
SN_API bool sn_ink_color_gray(uint32_t color, uint8_t* out_gray);
/* True for the five marker color spellings, including legacy codes. */
SN_API bool sn_ink_color_is_marker(uint32_t color);
/* The decimal BBB and FFF components of TITLESTYLE's 1BBBFFF spelling.
 * Returned as stored, without clamping to an eight-bit gray. */
SN_API int64_t sn_title_style_background(int64_t style);
SN_API int64_t sn_title_style_foreground(int64_t style);
/* Black-on-black is the format's cross-hatched title box. */
SN_API bool sn_title_style_is_hatched(int64_t style);
/* Static NUL-terminated metadata key, or null for an invalid enum. */
SN_API const char* sn_layer_name_key(sn_layer_name name);
/* Layer identifier: -1 background, 0 main, 1..3 ink; -2 for invalid enums. */
SN_API int64_t sn_layer_name_identifier(sn_layer_name name);
/* Portrait pixel dimensions; {0,0} for unknown equipment. */
SN_API sn_size sn_equipment_sheet(sn_equipment equipment);
#ifdef __cplusplus
}
#endif
#endif
