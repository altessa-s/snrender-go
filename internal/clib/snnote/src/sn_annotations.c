/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
/* Accept exactly four parseable comma-separated numbers; empty and nonnumeric
 * components are ignored, matching stored annotation handling. */
bool sn_rect_parse(sn_string text, sn_rect* out) {
  if (!text.data) return false;
  double values[4];
  size_t count = 0, start = 0;
  for (size_t index = 0; sn_work(1), (index <= text.length); index++)
    if (index == text.length || text.data[index] == ',') {
      if (index > start) {
        double number;
        sn_string part =
            sn_trim((sn_string){text.data + start, index - start}, false);
        if (sn_double(part, &number) && isfinite(number)) {
          if (count == 4) return false;
          values[count++] = number;
        }
      }
      start = index + 1;
    }
  if (count != 4) return false;
  *out = (sn_rect){values[0], values[1], values[2], values[3], false};
  return true;
}

/* Read an optional rectangle, retaining the fallback when it cannot be decoded.
 */
static sn_rect metadata_rect(sn_metadata metadata, const char* key,
                             sn_rect fallback) {
  (void)sn_rect_parse(sn_field_value(metadata, key), &fallback);
  return fallback;
}

/* Read at most four decimal page digits after the first underscore; malformed
 * annotation keys yield zero. */
static int64_t page_number(sn_string key) {
  size_t start = 0;
  while (sn_work(1), (start < key.length && key.data[start] != '_')) start++;
  if (start == key.length) return 0;
  start++;
  sn_string text = {key.data + start,
                    key.length - start < 4 ? key.length - start : 4};
  int64_t notebook = 0;
  (void)sn_integer(text, &notebook);
  return notebook;
}
/* Sort identity records, not the model arrays: replacement keeps the first
 * insertion position while taking the last matching value. */
typedef struct annotation_identity {
  sn_string key;
  sn_rect rect;
  size_t order;
} annotation_identity;

/* Null rectangles compare as zero rectangles. A NaN never equals another
 * rectangle, so its insertion index makes it a separate identity. */
static int compare_identity(const void* left, const void* right) {
  sn_work(1);
  const annotation_identity* first = left;
  const annotation_identity* second = right;
  size_t common = first->key.length < second->key.length ? first->key.length
                                                         : second->key.length;
  int comparison =
      common ? sn_work_memcmp(first->key.data, second->key.data, common) : 0;
  if (comparison) return comparison;
  if (first->key.length != second->key.length)
    return (first->key.length > second->key.length) ? 1 : -1;
  sn_rect first_rect = first->rect.is_null ? (sn_rect){0} : first->rect;
  sn_rect second_rect = second->rect.is_null ? (sn_rect){0} : second->rect;
  double first_values[] = {first_rect.x, first_rect.y, first_rect.width,
                           first_rect.height};
  double second_values[] = {second_rect.x, second_rect.y, second_rect.width,
                            second_rect.height};
  bool first_nan = false, second_nan = false;
  for (size_t index = 0; sn_work(1), (index < 4); index++) {
    first_nan |= isnan(first_values[index]);
    second_nan |= isnan(second_values[index]);
  }
  if (first_nan != second_nan) return first_nan ? 1 : -1;
  if (first_nan)
    return (first->order > second->order) - (first->order < second->order);
  for (size_t index = 0; sn_work(1), (index < 4); index++) {
    if (first_values[index] < second_values[index]) return -1;
    if (first_values[index] > second_values[index]) return 1;
  }
  return 0;
}

/* Tie-break with file order so the last duplicate wins with any sort order. */
static int compare_identity_order(const void* left, const void* right) {
  sn_work(1);
  int comparison = compare_identity(left, right);
  if (comparison) return comparison;
  const annotation_identity* first = left;
  const annotation_identity* second = right;
  return (first->order > second->order) - (first->order < second->order);
}

/* The array was allocated with count * width checked by sn_alloc. Sorting
 * identities avoids an attacker-chosen scan through every earlier record. */
static size_t deduplicate(sn_arena* arena, void* records, size_t count,
                          size_t width, annotation_identity* identities) {
  bool* kept = sn_alloc(arena, count, sizeof *kept);
  if (!kept) return count;
  sn_work_sort(identities, count, sizeof *identities, compare_identity_order);
  uint8_t* bytes = records;
  for (size_t first = 0; sn_work(1), (first < count);) {
    size_t end = first + 1;
    while (sn_work(1), (end < count && compare_identity(identities + first,
                                                        identities + end) == 0))
      end++;
    size_t destination = identities[first].order;
    size_t source = identities[end - 1].order;
    if (destination != source)
      sn_work_memcpy(bytes + destination * width, bytes + source * width,
                     width);
    kept[destination] = true;
    first = end;
  }
  size_t written = 0;
  for (size_t index = 0; sn_work(1), (index < count); index++) {
    if (!kept[index]) continue;
    if (written != index)
      sn_work_memmove(bytes + written * width, bytes + index * width, width);
    written++;
  }
  return written;
}

/* Copy trimmed annotation text so it has its own trailing terminator. */
static sn_string copy_trimmed(sn_arena* arena, sn_string text) {
  text = sn_trim(text, true);
  return sn_text(arena, (const uint8_t*)text.data, text.length);
}

/* Decode optional Base64 text. Strict display text retains the original
 * spelling when decoding would repair UTF-8. */
static sn_string decode_annotation_text(sn_arena* arena, sn_string text,
                                        bool strict, bool fallback) {
  sn_bytes bytes;
  if (!text.data) return text;
  sn_arena scratch = {.budget = arena->budget};
  if (!sn_base64(&scratch, (sn_bytes){(const uint8_t*)text.data, text.length},
                 false, &bytes)) {
    sn_scratch_free(arena, &scratch);
    return fallback ? text : (sn_string){0};
  }
  if (strict && bytes.size >= 3 &&
      !sn_work_memcmp(bytes.data, "\xef\xbb\xbf", 3)) {
    bytes.data += 3;
    bytes.size -= 3;
  }
  sn_string result = sn_text(arena, bytes.data, bytes.size);
  if (!result.data) {
    sn_scratch_free(arena, &scratch);
    return (sn_string){0};
  }
  if (strict && (result.length != bytes.size ||
                 sn_work_memcmp(result.data, bytes.data, bytes.size)))
    result = text;
  sn_scratch_free(arena, &scratch);
  return result;
}

/* Read optional footer records independently, then resolve duplicates without
 * changing insertion order; bad blocks are diagnosed and skipped. */
void sn_annotations(sn_arena* arena, sn_bytes file, sn_notebook* notebook,
                    sn_metadata header) {
  size_t capacity = notebook->footer.count;
  size_t title_capacity = 0, keyword_capacity = 0, link_capacity = 0,
         template_capacity = 0;
  for (size_t index = 0; sn_work(1), (index < capacity); index++) {
    const char* key = notebook->footer.entries[index].key.data;
    sn_work(24);
    title_capacity += !strncmp(key, "TITLE_", 6);
    keyword_capacity += !strncmp(key, "KEYWORD_", 8);
    link_capacity += !strncmp(key, "LINK", 4);
    template_capacity += !strncmp(key, "STYLE_", 6);
  }
  sn_title* titles = sn_alloc(arena, title_capacity, sizeof *titles);
  sn_keyword* keywords = sn_alloc(arena, keyword_capacity, sizeof *keywords);
  sn_link* links = sn_alloc(arena, link_capacity, sizeof *links);
  sn_template* templates =
      sn_alloc(arena, template_capacity, sizeof *templates);
  annotation_identity* title_keys =
      sn_alloc(arena, title_capacity, sizeof *title_keys);
  annotation_identity* keyword_keys =
      sn_alloc(arena, keyword_capacity, sizeof *keyword_keys);
  annotation_identity* template_keys =
      sn_alloc(arena, template_capacity, sizeof *template_keys);
  if (arena->failed) return;
  for (size_t footer_index = 0; sn_work(1), (footer_index < capacity);
       footer_index++) {
    sn_metadata_entry entry = notebook->footer.entries[footer_index];
    sn_bytes block = {0};
    size_t annotation_kind = 0;
    int64_t address = 0;
    (void)sn_integer(entry.value, &address);
    const char* key = entry.key.data;
    sn_work(12);
    if (!strncmp(key, "STYLE_", 6) || !strncmp(key, "COVER_", 6)) {
      if (key[0] == 'C' && notebook->cover) continue;
      sn_bytes bytes;
      if (!sn_block(file, address, &bytes)) goto bad;
      if (!bytes.data) continue;
      const sn_bitmap* bitmap = sn_bitmap_decode(arena, bytes, (sn_string){0});
      if (!bitmap) return;
      if (key[0] == 'S') {
        sn_string name =
            sn_text(arena, (const uint8_t*)key + 6, entry.key.length - 6);
        size_t index = notebook->template_count++;
        templates[index] = (sn_template){name, *bitmap};
        template_keys[index] =
            (annotation_identity){.key = name, .order = index};
      } else if (!notebook->cover) {
        notebook->cover = bitmap;
        (void)sn_integer((sn_string){key + 6, entry.key.length - 6},
                         &notebook->cover_type);
      }
      continue;
    }
    sn_work(18);
    bool is_title = !strncmp(key, "TITLE_", 6),
         is_keyword = !strncmp(key, "KEYWORD_", 8),
         is_link = !strncmp(key, "LINK", 4);
    if (!is_title && !is_keyword && !is_link) continue;
    annotation_kind = is_title     ? SN_CACHE_TITLE
                      : is_keyword ? SN_CACHE_KEYWORD
                                   : SN_CACHE_LINK;
    if (!sn_block(file, address, &block)) goto bad;
    if (sn_cached(arena, sn_offset(arena, block.data), SN_CACHE_INVALID,
                  annotation_kind))
      goto report_bad;
    sn_metadata metadata;
    if (!sn_metadata_at(arena, file, address, &metadata)) goto bad;
    sn_metadata full_metadata = metadata;
    metadata = sn_fields(arena, sn_offset(arena, block.data), metadata);
    sn_rect null = {.is_null = true};
    if (is_title) {
      sn_title title = {.page_number = page_number(entry.key),
                        .metadata = full_metadata};
      sn_title* cached =
          sn_cached(arena, sn_offset(arena, block.data), SN_CACHE_TITLE, 0);
      if (cached) {
        title = *cached;
        title.page_number = page_number(entry.key);
        goto title_ready;
      }
      sn_decode_begin(arena, sn_offset(arena, block.data), SN_CACHE_TITLE, 0);
      title.level = sn_field_number(metadata, "TITLELEVEL", 1);
      title.style = sn_field_number(metadata, "TITLESTYLE", 0);
      title.rect = metadata_rect(metadata, "TITLERECT", null);
      title.original_rect = metadata_rect(metadata, "TITLERECTORI", title.rect);
      if (!sn_bitmap_at(arena, file, metadata, "TITLEBITMAP", "TITLEPROTOCOL",
                        &title.bitmap))
        goto bad;
      sn_cache_put(arena, sn_offset(arena, block.data), SN_CACHE_TITLE, 0,
                   &title, sizeof title);
    title_ready:
      size_t index = notebook->title_count++;
      titles[index] = title;
      title_keys[index] = (annotation_identity){entry.key, title.rect, index};
    } else if (is_keyword) {
      sn_keyword keyword = {.metadata = full_metadata};
      sn_keyword* cached =
          sn_cached(arena, sn_offset(arena, block.data), SN_CACHE_KEYWORD, 0);
      if (cached) {
        keyword = *cached;
      }
      if (cached) goto keyword_ready;
      sn_decode_begin(arena, sn_offset(arena, block.data), SN_CACHE_KEYWORD, 0);
      keyword.page_number = sn_field_number(metadata, "KEYWORDPAGE", 0);
      keyword.rect = metadata_rect(metadata, "KEYWORDRECT", null);
      keyword.original_rect =
          metadata_rect(metadata, "KEYWORDRECTORI", keyword.rect);
      sn_bytes bytes;
      if (!sn_block(file, sn_field_number(metadata, "KEYWORDSITE", 0), &bytes))
        goto bad;
      if (bytes.data) {
        sn_string* cached_text =
            sn_cached(arena, sn_offset(arena, bytes.data), SN_CACHE_TEXT, 0);
        if (cached_text)
          keyword.text = *cached_text;
        else {
          sn_decode_begin(arena, sn_offset(arena, bytes.data), SN_CACHE_TEXT,
                          0);
          size_t len = 0;
          while (sn_work(1), (len < bytes.size && bytes.data[len])) len++;
          keyword.text = copy_trimmed(arena, sn_text(arena, bytes.data, len));
          sn_cache_put(arena, sn_offset(arena, bytes.data), SN_CACHE_TEXT, 0,
                       &keyword.text, sizeof keyword.text);
        }
      } else
        keyword.text = sn_field_value(metadata, "KEYWORD");
      if (!keyword.text.data)
        keyword.text = sn_text(arena, (const uint8_t*)"", 0);
      if (!bytes.data) keyword.text = copy_trimmed(arena, keyword.text);
      sn_cache_put(arena, sn_offset(arena, block.data), SN_CACHE_KEYWORD, 0,
                   &keyword, sizeof keyword);
    keyword_ready:
      if (keyword.page_number <= 0)
        keyword.page_number = page_number(entry.key);
      size_t index = notebook->keyword_count++;
      keywords[index] = keyword;
      keyword_keys[index] =
          (annotation_identity){entry.key, keyword.rect, index};
    } else {
      sn_link link = {.page_number = page_number(entry.key),
                      .metadata = full_metadata};
      sn_link* cached =
          sn_cached(arena, sn_offset(arena, block.data), SN_CACHE_LINK, 0);
      if (cached) {
        link = *cached;
        link.page_number = page_number(entry.key);
        goto link_ready;
      }
      sn_decode_begin(arena, sn_offset(arena, block.data), SN_CACHE_LINK, 0);
      link.rect = metadata_rect(metadata, "LINKRECT", null);
      link.kind = sn_field_number(metadata, "LINKTYPE", 0);
      link.direction = sn_field_number(metadata, "LINKINOUT", 0);
      link.style = sn_field_number(metadata, "LINKSTYLE", 0);
      link.target_path = decode_annotation_text(
          arena, sn_unset(sn_field_value(metadata, "LINKFILE")), false, false);
      link.target_file_id = sn_unset(sn_field_value(metadata, "LINKFILEID"));
      link.target_page_id = sn_unset(sn_field_value(metadata, "PAGEID"));
      link.target_page_number = sn_field_number(metadata, "OBJPAGE", 0);
      link.has_target_page_number = link.target_page_number > 0;
      link.timestamp = sn_unset(sn_field_value(metadata, "LINKTIMESTAMP"));
      sn_string text = sn_unset(sn_field_value(metadata, "SHOWTEXT"));
      if (!text.data) text = sn_unset(sn_field_value(metadata, "FULLTEXT"));
      link.text = decode_annotation_text(arena, text, true, true);
      if (link.kind == 4) link.url = link.target_path;
      if (!sn_bitmap_at(arena, file, metadata, "LINKBITMAP", "LINKPROTOCAL",
                        &link.bitmap))
        goto bad;
      sn_cache_put(arena, sn_offset(arena, block.data), SN_CACHE_LINK, 0, &link,
                   sizeof link);
    link_ready:
      links[notebook->link_count++] = link;
    }
    continue;
  bad:
    if (annotation_kind && block.data) {
      bool invalid = true;
      sn_cache_put(arena, sn_offset(arena, block.data), SN_CACHE_INVALID,
                   annotation_kind, &invalid, sizeof invalid);
    }
  report_bad:
    sn_log_writef(SN_LOG_ERROR, "footer annotation does not read");
  }
  notebook->title_count = deduplicate(arena, titles, notebook->title_count,
                                      sizeof *titles, title_keys);
  notebook->keyword_count = deduplicate(
      arena, keywords, notebook->keyword_count, sizeof *keywords, keyword_keys);
  notebook->template_count =
      deduplicate(arena, templates, notebook->template_count, sizeof *templates,
                  template_keys);
  notebook->titles = titles;
  notebook->keywords = keywords;
  notebook->links = links;
  notebook->templates = templates;
  sn_bytes highlight;
  if (sn_block(file, sn_field_number(header, "HIGHLIGHTINFO", 0), &highlight) &&
      highlight.data)
    sn_highlights(arena, highlight, notebook);
}
