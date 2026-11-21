/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef SN_INTERNAL_H
#define SN_INTERNAL_H
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snnote/snnote.h"
typedef struct sn_chunk sn_chunk;
typedef struct sn_work_meter {
  size_t used, limit;
  jmp_buf escape;
  sn_chunk* chunks;
  void* owner;
} sn_work_meter;
extern _Thread_local sn_work_meter* sn_meter;
/* Hot scanners bind the meter once. Keep every charge in meter storage and
 * check before incrementing, with the same per-step granularity as sn_work.
 * A cached binding is local to a scanner, never carried into a host callback.
 */
static inline void sn_work_on(sn_work_meter* meter, size_t bytes) {
  if (!meter) return;
  if (bytes > meter->limit - meter->used) longjmp(meter->escape, 1);
  meter->used += bytes;
}
void sn_work(size_t bytes);
void sn_work_release(sn_work_meter* meter, bool discard);
void* sn_work_memmove(void* out, const void* input, size_t size);
void* sn_work_memcpy(void* out, const void* input, size_t size);
void* sn_work_memset(void* out, int value, size_t size);
int sn_work_memcmp(const void* a, const void* b, size_t size);
void sn_work_sort(void* base, size_t count, size_t width,
                  int (*compare)(const void*, const void*));
typedef struct sn_budget {
  size_t limit, live, peak;
  bool exceeded;
} sn_budget;
typedef enum sn_cache_kind : int {
  SN_CACHE_METADATA = 1,
  SN_CACHE_PAGE = 2,
  SN_CACHE_RECOGNITION = 3,
  SN_CACHE_STROKES = 4,
  SN_CACHE_TITLE = 5,
  SN_CACHE_KEYWORD = 6,
  SN_CACHE_LINK = 7,
  SN_CACHE_BITMAP = 8,
  SN_CACHE_TEXT = 9,
  SN_CACHE_LAYER = 10,
  SN_CACHE_SHEET = 11,
  SN_CACHE_INVALID = 12,
  SN_CACHE_COVERAGE = 13,
  SN_CACHE_BITMAP_VIEW = 14,
  SN_CACHE_FIELDS = 15,
  SN_CACHE_PROTOCOL = 16,
} sn_cache_kind;
typedef struct sn_cache sn_cache;
typedef struct sn_arena {
  sn_chunk* head;
  bool failed;
  sn_budget* budget;
  sn_cache* cache;
  size_t cache_count, protocol_count;
  const uint8_t* file_base;
  size_t file_size;
} sn_arena;
void* sn_cached(sn_arena* arena, size_t key, size_t kind, size_t context);
void sn_cache_put(sn_arena* arena, size_t key, size_t kind, size_t context,
                  const void* value, size_t size);
size_t sn_offset(const sn_arena* arena, const uint8_t* data);
size_t sn_protocol(sn_arena* arena, sn_string protocol);
sn_metadata sn_fields(sn_arena* arena, size_t offset, sn_metadata metadata);
sn_string sn_field_value(sn_metadata fields, const char* key);
int64_t sn_field_number(sn_metadata fields, const char* key, int64_t fallback);
void sn_scratch_free(sn_arena* arena, sn_arena* scratch);
void* sn_alloc(sn_arena* arena, size_t count, size_t width);
void sn_arena_free(sn_arena* arena);
sn_string sn_text(sn_arena* arena, const uint8_t* input, size_t count);
bool sn_equal(sn_string text, const char* c);
sn_string sn_unset(sn_string text);
sn_string sn_trim(sn_string text, bool newlines);
bool sn_double(sn_string text, double* out);
bool sn_integer(sn_string text, int64_t* out);
sn_status sn_error_set(sn_error* error, sn_status text, int64_t value);
sn_status sn_error_at(sn_error* error, sn_status status, int64_t value,
                      const char* detail);
typedef struct sn_reader {
  const uint8_t* data;
  size_t size, pos;
  bool bad;
} sn_reader;
uint32_t sn_u32(sn_reader* reader);
uint16_t sn_u16(sn_reader* reader);
float sn_f32(sn_reader* reader);
sn_bytes sn_take(sn_reader* reader, size_t count);
size_t sn_count(sn_reader* reader, size_t stride);
sn_status sn_block_status(sn_bytes file, int64_t address);
bool sn_block(sn_bytes file, int64_t address, sn_bytes* out);
sn_metadata sn_metadata_read(sn_arena* arena, sn_bytes bytes);
bool sn_metadata_at(sn_arena* arena, sn_bytes file, int64_t address,
                    sn_metadata* out);
sn_string sn_value(sn_metadata metadata, const char* key);
int64_t sn_number(sn_metadata metadata, const char* key, int64_t fallback);
bool sn_base64(sn_arena* arena, sn_bytes input, bool stored, sn_bytes* out);
bool sn_lz4(sn_arena* arena, sn_bytes input, size_t expanded, sn_bytes* out);
typedef enum sn_rle_result : int {
  SN_RLE_UNDERSHOOT = -1,
  SN_RLE_COMPLETE = 0,
  SN_RLE_OVERSHOOT = 1
} sn_rle_result;
constexpr size_t SN_RLE_BLOCK_SIZE = 0x4000;
constexpr size_t SN_RLE_BLANK_BLOCK_SIZE = 0x400;
sn_rle_result sn_rle(sn_bytes bytes, size_t total, size_t block,
                     uint8_t* output);
bool sn_fits(sn_bytes bytes, size_t total);
sn_bitmap sn_bitmap_make(sn_bytes bytes, sn_string protocol);
const sn_bitmap* sn_bitmap_decode(sn_arena* arena, sn_bytes bytes,
                                  sn_string protocol);
bool sn_bitmap_at(sn_arena* arena, sn_bytes file, sn_metadata metadata,
                  const char* key, const char* protocol, const sn_bitmap** out);
sn_equipment sn_equipment_of(sn_string text);
sn_size sn_sheet(sn_string text);
extern const sn_size sn_sheets[2];
constexpr size_t SN_SHEET_COUNT = 2;

#ifdef SNNOTE_TESTING
size_t sn_test_log_allocations(void);
void sn_test_alloc_fail_after(size_t count);
bool sn_test_alloc_did_fail(void);
size_t sn_test_arena_peak(void);
void sn_test_peak_reset(void);
void sn_test_work_reset(void);
size_t sn_test_work_used(void);
size_t sn_test_work_limit(void);
void sn_test_work_finish(const sn_work_meter* meter);
size_t sn_test_cache_probes(void);
size_t sn_test_decodes(void);
size_t sn_test_kind_decodes(sn_cache_kind kind);
void sn_decode_begin(sn_arena* arena, size_t key, sn_cache_kind kind,
                     size_t context);
size_t sn_test_duplicate_decodes(void);
bool sn_test_cache_walk(sn_arena* arena);
extern _Thread_local size_t sn_test_metadata_comparisons;
size_t sn_test_decimal_operations(void);
void sn_test_decimal_reset(void);
bool sn_test_double_fallback(sn_string text, double* out);
#else
static inline void sn_decode_begin(sn_arena* arena, size_t key,
                                   sn_cache_kind kind, size_t context) {
  (void)arena;
  (void)key;
  (void)kind;
  (void)context;
}
#endif
bool sn_declared_fits(const sn_arena* arena, size_t count, size_t width);
void* sn_calloc(size_t count, size_t width);
#if defined(__has_attribute)
#if __has_attribute(format)
__attribute__((format(printf, 2, 3)))
#endif
#endif
void sn_log_writef(sn_log_level level, const char* format, ...);
bool sn_log_wanted(void);
void sn_classify(sn_stroke* text);
void sn_strokes(sn_arena* arena, sn_bytes bytes, sn_page* page);
void sn_derived(sn_arena* arena, sn_page* page);
bool sn_rect_parse(sn_string text, sn_rect* out);
void sn_annotations(sn_arena* arena, sn_bytes file, sn_notebook* count,
                    sn_metadata header);
/* JSON children form a sibling list; nesting is bounded independently of
 * document breadth. */
typedef enum sn_json_kind : int {
  SN_JNULL,
  SN_JBOOL,
  SN_JNUMBER,
  SN_JSTRING,
  SN_JARRAY,
  SN_JOBJECT
} sn_json_kind;
typedef struct sn_json {
  sn_json_kind kind;
  sn_string key, string;
  bool boolean;
  struct sn_json* child;
  struct sn_json* next;
  size_t count, order;
} sn_json;
sn_json* sn_json_read(sn_arena* arena, sn_bytes bytes);
const sn_json* sn_json_get(const sn_json* node, const char* key);
void sn_layer_table(sn_arena* arena, sn_string text, sn_page* page);
const sn_recognition* sn_recognition_read(sn_arena* arena, sn_bytes bytes);
void sn_highlights(sn_arena* arena, sn_bytes bytes, sn_notebook* count);
#endif
