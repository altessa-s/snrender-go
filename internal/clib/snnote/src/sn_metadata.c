/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
/* Collect complete nonempty tags in file order, preserving repeated keys and
 * colons in values. */
sn_metadata sn_metadata_read(sn_arena* arena, sn_bytes bytes) {
  sn_work_meter* meter = sn_meter;
  /* Count complete tags first. Capacity is backed by actual delimiters,
   * never by a count supplied by the file. */
  size_t count = 0;
  for (size_t index = 0; sn_work_on(meter, 1), (index < bytes.size); index++)
    if (bytes.data[index] == '>') count++;
  sn_metadata_entry* entries = sn_alloc(arena, count, sizeof *entries);
  sn_metadata metadata = {entries, 0};
  if (!entries) return metadata;
  size_t index = 0;
  while (sn_work_on(meter, 1), (index < bytes.size)) {
    if (bytes.data[index] != '<') {
      index++;
      continue;
    }
    size_t colon = index + 1;
    while (sn_work_on(meter, 1),
           (colon < bytes.size && bytes.data[colon] != ':' &&
            bytes.data[colon] != '<' && bytes.data[colon] != '>'))
      colon++;
    if (colon == bytes.size || bytes.data[colon] != ':') {
      index = colon;
      continue;
    }
    size_t close = colon + 1;
    while (sn_work_on(meter, 1),
           (close < bytes.size && bytes.data[close] != '>' &&
            bytes.data[close] != '<'))
      close++;
    if (close == bytes.size || bytes.data[close] != '>') {
      index = close;
      continue;
    }
    if (colon > index + 1)
      entries[metadata.count++] = (sn_metadata_entry){
          sn_text(arena, bytes.data + index + 1, colon - index - 1),
          sn_text(arena, bytes.data + colon + 1, close - colon - 1)};
    index = close + 1;
  }
  return metadata;
}

/* Resolve and decode metadata into the arena; allocation failure is never
 * successful recovery. */
bool sn_metadata_at(sn_arena* arena, sn_bytes file, int64_t address,
                    sn_metadata* out) {
  sn_bytes bytes;
  if (!sn_block(file, address, &bytes)) return false;
  sn_metadata* cached =
      sn_cached(arena, sn_offset(arena, bytes.data), SN_CACHE_METADATA, 0);
  if (cached) {
    *out = *cached;
    return true;
  }
  sn_decode_begin(arena, sn_offset(arena, bytes.data), SN_CACHE_METADATA, 0);
  *out = sn_metadata_read(arena, bytes);
  sn_cache_put(arena, sn_offset(arena, bytes.data), SN_CACHE_METADATA, 0, out,
               sizeof *out);
  return !arena->failed;
}

/* Advance the caller cursor through repeated bytewise keys; null arguments
 * return no match. */
const sn_string* sn_metadata_next(const sn_metadata* metadata, const char* key,
                                  size_t* index) {
  if (!metadata || !key || !index) return nullptr;
  while (sn_work(1), (*index < metadata->count)) {
    const sn_metadata_entry* entry = &metadata->entries[(*index)++];
#ifdef SNNOTE_TESTING
    sn_test_metadata_comparisons++;
#endif
    if (sn_equal(entry->key, key)) return &entry->value;
  }
  return nullptr;
}

/* Return the first matching borrowed value, sharing the repeated-key iterator
 * semantics. */
const sn_string* sn_metadata_get(const sn_metadata* metadata, const char* key) {
  size_t index = 0;
  return sn_metadata_next(metadata, key, &index);
}

/* Return a borrowed value or the absent-string sentinel. */
sn_string sn_value(sn_metadata metadata, const char* key) {
  const sn_string* text = sn_metadata_get(&metadata, key);
  return text ? *text : (sn_string){0};
}

/* Decode a trimmed integer field, preserving the supplied fallback for absent
 * or malformed values. */
int64_t sn_number(sn_metadata metadata, const char* key, int64_t fallback) {
  int64_t count;
  return sn_integer(sn_trim(sn_value(metadata, key), false), &count) ? count
                                                                     : fallback;
}

/* Fixed parser fields, sorted bytewise. Collect first occurrences in one pass
 * over the block, then compact this small view. Public metadata stays ordered
 * and sn_metadata_get remains linear. Unknown entries never enter the view. */
static const char* const field_names[] = {
    "APPLY_EQUIPMENT", "BGLAYER",        "DISABLE",       "FILE_ID",
    "FULLTEXT",        "HIGHLIGHTINFO",  "KEYWORD",       "KEYWORDPAGE",
    "KEYWORDRECT",     "KEYWORDRECTORI", "KEYWORDSITE",   "LAYER1",
    "LAYER2",          "LAYER3",         "LAYERBITMAP",   "LAYERINFO",
    "LAYERPROTOCOL",   "LAYERSEQ",       "LINKBITMAP",    "LINKFILE",
    "LINKFILEID",      "LINKINOUT",      "LINKPROTOCAL",  "LINKRECT",
    "LINKSTYLE",       "LINKTIMESTAMP",  "LINKTYPE",      "MAINLAYER",
    "OBJPAGE",         "ORIENTATION",    "PAGEID",        "PAGEMACHINETYPE",
    "PAGESTYLE",       "RECOGNFILE",     "RECOGNTEXT",    "SHOWTEXT",
    "TITLEBITMAP",     "TITLELEVEL",     "TITLEPROTOCOL", "TITLERECT",
    "TITLERECTORI",    "TITLESTYLE",     "TOTALPATH",
};
static int compare_text(sn_string a, sn_string b) {
#ifdef SNNOTE_TESTING
  sn_test_metadata_comparisons++;
#endif
  size_t length = a.length < b.length ? a.length : b.length;
  int order = sn_work_memcmp(a.data, b.data, length);
  return order ? order : (a.length > b.length) - (a.length < b.length);
}
sn_metadata sn_fields(sn_arena* arena, size_t offset, sn_metadata metadata) {
  sn_metadata* cached = sn_cached(arena, offset, SN_CACHE_FIELDS, 0);
  if (cached) return *cached;
  sn_decode_begin(arena, offset, SN_CACHE_FIELDS, 0);
  sn_metadata_entry fields[sizeof field_names / sizeof *field_names] = {0};
  size_t capacity = sizeof fields / sizeof *fields;
  for (size_t index = 0; sn_work(1), (index < metadata.count); index++) {
    sn_metadata_entry entry = metadata.entries[index];
    size_t low = 0, high = capacity;
    while (sn_work(1), (low < high)) {
      size_t middle = low + (high - low) / 2;
      int order = compare_text(
          entry.key,
          (sn_string){field_names[middle], strlen(field_names[middle])});
      if (!order) {
        if (!fields[middle].key.data) fields[middle] = entry;
        break;
      }
      if (order < 0)
        high = middle;
      else
        low = middle + 1;
    }
  }
  size_t count = 0;
  for (size_t index = 0; sn_work(1), (index < capacity); index++)
    if (fields[index].key.data) fields[count++] = fields[index];
  sn_metadata_entry* entries = sn_alloc(arena, count, sizeof *entries);
  if (!entries) return (sn_metadata){0};
  sn_work_memcpy(entries, fields, count * sizeof *entries);
  sn_metadata result = {entries, count};
  sn_cache_put(arena, offset, SN_CACHE_FIELDS, 0, &result, sizeof result);
  return result;
}
sn_string sn_field_value(sn_metadata fields, const char* key) {
  size_t low = 0, high = fields.count;
  sn_string target = {key, strlen(key)};
  while (sn_work(1), (low < high)) {
    size_t middle = low + (high - low) / 2;
    int order = compare_text(target, fields.entries[middle].key);
    if (!order) return fields.entries[middle].value;
    if (order < 0)
      high = middle;
    else
      low = middle + 1;
  }
  return (sn_string){0};
}
int64_t sn_field_number(sn_metadata fields, const char* key, int64_t fallback) {
  int64_t value;
  return sn_integer(sn_trim(sn_field_value(fields, key), false), &value)
             ? value
             : fallback;
}
