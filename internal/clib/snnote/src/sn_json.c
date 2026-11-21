/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
typedef struct parser {
  sn_arena* arena;
  sn_work_meter* meter;
  sn_bytes bytes;
  size_t pos;
  bool bad;
} parser;
/* Consume exactly the four whitespace bytes permitted by JSON. */
static void skip_space(parser* parser_state) {
  while (sn_work_on(parser_state->meter, 1),
         (parser_state->pos < parser_state->bytes.size &&
          (parser_state->bytes.data[parser_state->pos] == 32 ||
           parser_state->bytes.data[parser_state->pos] == 9 ||
           parser_state->bytes.data[parser_state->pos] == 10 ||
           parser_state->bytes.data[parser_state->pos] == 13)))
    parser_state->pos++;
}

/* Read four hexadecimal digits for a Unicode escape; truncation or nonhex input
 * marks the parser bad. */
static unsigned read_hex_quad(parser* parser_state) {
  unsigned scalar = 0;
  for (int index = 0; sn_work_on(parser_state->meter, 1), (index < 4);
       index++) {
    if (parser_state->pos == parser_state->bytes.size) {
      parser_state->bad = true;
      return 0;
    }
    unsigned digit = parser_state->bytes.data[parser_state->pos++];
    if (digit >= '0' && digit <= '9')
      digit -= '0';
    else if (digit >= 'a' && digit <= 'f')
      digit = digit - 'a' + 10;
    else if (digit >= 'A' && digit <= 'F')
      digit = digit - 'A' + 10;
    else {
      parser_state->bad = true;
      return 0;
    }
    scalar = scalar * 16 + digit;
  }
  return scalar;
}

/* Decode escapes and surrogate pairs, then require valid UTF-8. Escapes cannot
 * expand beyond the reserved source-byte extent. */
static sn_string read_json_string(parser* parser_state) {
  size_t start = ++parser_state->pos, end = start;
  bool escape = false;
  while (sn_work_on(parser_state->meter, 1), (end < parser_state->bytes.size)) {
    uint8_t character = parser_state->bytes.data[end++];
    if (!escape && character == '"') break;
    if (!escape && character == '\\')
      escape = true;
    else
      escape = false;
  }
  if (end == start || end > parser_state->bytes.size ||
      parser_state->bytes.data[end - 1] != '"') {
    parser_state->bad = true;
    return (sn_string){0};
  }
  uint8_t* buf = sn_alloc(parser_state->arena, end - start + 1, 1);
  if (!buf) {
    parser_state->bad = true;
    return (sn_string){0};
  }
  size_t written = 0;
  while (sn_work_on(parser_state->meter, 1),
         (parser_state->pos < end - 1 && !parser_state->bad)) {
    uint8_t character = parser_state->bytes.data[parser_state->pos++];
    if (character < 32) {
      parser_state->bad = true;
      break;
    }
    if (character != '\\') {
      buf[written++] = character;
      continue;
    }
    if (parser_state->pos == end - 1) {
      parser_state->bad = true;
      break;
    }
    character = parser_state->bytes.data[parser_state->pos++];
    if (character == 'u') {
      unsigned scalar = read_hex_quad(parser_state);
      if (scalar >= 0xd800 && scalar <= 0xdbff) {
        if (parser_state->pos > end - 1 || end - 1 - parser_state->pos < 2 ||
            parser_state->bytes.data[parser_state->pos] != '\\' ||
            parser_state->bytes.data[parser_state->pos + 1] != 'u') {
          parser_state->bad = true;
          break;
        }
        parser_state->pos += 2;
        unsigned low_surrogate = read_hex_quad(parser_state);
        if (low_surrogate < 0xdc00 || low_surrogate > 0xdfff) {
          parser_state->bad = true;
          break;
        }
        scalar = 0x10000 + ((scalar - 0xd800) << 10) + (low_surrogate - 0xdc00);
      } else if (scalar >= 0xdc00 && scalar <= 0xdfff) {
        parser_state->bad = true;
        break;
      }
      if (scalar < 0x80)
        buf[written++] = (uint8_t)scalar;
      else if (scalar < 0x800) {
        buf[written++] = (uint8_t)(0xc0 | (scalar >> 6));
        buf[written++] = (uint8_t)(0x80 | (scalar & 63));
      } else if (scalar < 0x10000) {
        buf[written++] = (uint8_t)(0xe0 | (scalar >> 12));
        buf[written++] = (uint8_t)(0x80 | ((scalar >> 6) & 63));
        buf[written++] = (uint8_t)(0x80 | (scalar & 63));
      } else {
        buf[written++] = (uint8_t)(0xf0 | (scalar >> 18));
        buf[written++] = (uint8_t)(0x80 | ((scalar >> 12) & 63));
        buf[written++] = (uint8_t)(0x80 | ((scalar >> 6) & 63));
        buf[written++] = (uint8_t)(0x80 | (scalar & 63));
      }
    } else {
      switch (character) {
        case '"':
        case '\\':
        case '/':
          buf[written++] = character;
          break;
        case 'b':
          buf[written++] = 8;
          break;
        case 'f':
          buf[written++] = 12;
          break;
        case 'n':
          buf[written++] = 10;
          break;
        case 'r':
          buf[written++] = 13;
          break;
        case 't':
          buf[written++] = 9;
          break;
        default:
          parser_state->bad = true;
          break;
      }
    }
  }
  parser_state->pos = end;
  sn_string result = sn_text(parser_state->arena, buf, written);
  if (!result.data || result.length != written ||
      sn_work_memcmp(result.data, buf, written))
    parser_state->bad = true;
  return result;
}

/* Parse a bounded-depth JSON value while retaining numeric spelling for exact
 * integer fields. Trailing commas are accepted. */
static sn_json* read_json_value(parser* parser_state, size_t depth) {
  skip_space(parser_state);
  if (parser_state->pos == parser_state->bytes.size ||
      depth > SN_JSON_MAX_DEPTH) {
    parser_state->bad = true;
    return nullptr;
  }
  sn_json* node = sn_alloc(parser_state->arena, 1, sizeof *node);
  if (!node) {
    parser_state->bad = true;
    return nullptr;
  }
  uint8_t character = parser_state->bytes.data[parser_state->pos];
  if (character == '"') {
    node->kind = SN_JSTRING;
    node->string = read_json_string(parser_state);
    return node;
  }
  if (character == '[' || character == '{') {
    if (depth == SN_JSON_MAX_DEPTH) {
      parser_state->bad = true;
      return node;
    }
    node->kind = character == '[' ? SN_JARRAY : SN_JOBJECT;
    uint8_t close = character == '[' ? ']' : '}';
    parser_state->pos++;
    skip_space(parser_state);
    sn_json** tail = &node->child;
    if (parser_state->pos < parser_state->bytes.size &&
        parser_state->bytes.data[parser_state->pos] == close) {
      parser_state->pos++;
      return node;
    }
    while (sn_work_on(parser_state->meter, 1), (!parser_state->bad)) {
      sn_string key = {0};
      if (character == '{') {
        skip_space(parser_state);
        if (parser_state->pos == parser_state->bytes.size ||
            parser_state->bytes.data[parser_state->pos] != '"') {
          parser_state->bad = true;
          break;
        }
        key = read_json_string(parser_state);
        skip_space(parser_state);
        if (parser_state->pos == parser_state->bytes.size ||
            parser_state->bytes.data[parser_state->pos++] != ':') {
          parser_state->bad = true;
          break;
        }
      }
      sn_json* child = read_json_value(parser_state, depth + 1);
      if (!child) break;
      child->key = key;
      child->order = node->count;
      *tail = child;
      tail = &child->next;
      node->count++;
      skip_space(parser_state);
      if (parser_state->pos == parser_state->bytes.size) {
        parser_state->bad = true;
        break;
      }
      uint8_t next = parser_state->bytes.data[parser_state->pos++];
      if (next == close) break;
      if (next != ',') {
        parser_state->bad = true;
        break;
      }
      skip_space(parser_state);
      if (parser_state->pos < parser_state->bytes.size &&
          parser_state->bytes.data[parser_state->pos] == close) {
        parser_state->pos++;
        break;
      }
    }
    return node;
  }
  const char* literal = character == 't'   ? "true"
                        : character == 'f' ? "false"
                        : character == 'n' ? "null"
                                           : nullptr;
  if (literal) {
    size_t written = strlen(literal);
    if (written > parser_state->bytes.size - parser_state->pos ||
        sn_work_memcmp(parser_state->bytes.data + parser_state->pos, literal,
                       written))
      parser_state->bad = true;
    else
      parser_state->pos += written;
    node->kind = character == 'n' ? SN_JNULL : SN_JBOOL;
    node->boolean = character == 't';
    return node;
  }
  size_t start = parser_state->pos;
  if (character == '-') parser_state->pos++;
  if (parser_state->pos == parser_state->bytes.size) {
    parser_state->bad = true;
    return node;
  }
  character = parser_state->bytes.data[parser_state->pos];
  if (character == '0')
    parser_state->pos++;
  else if (character >= '1' && character <= '9') {
    do {
      parser_state->pos++;
    } while (sn_work_on(parser_state->meter, 1),
             (parser_state->pos < parser_state->bytes.size &&
              parser_state->bytes.data[parser_state->pos] >= '0' &&
              parser_state->bytes.data[parser_state->pos] <= '9'));
  } else {
    parser_state->bad = true;
    return node;
  }
  if (parser_state->pos < parser_state->bytes.size &&
      parser_state->bytes.data[parser_state->pos] == '.') {
    parser_state->pos++;
    size_t begin = parser_state->pos;
    while (sn_work_on(parser_state->meter, 1),
           (parser_state->pos < parser_state->bytes.size &&
            parser_state->bytes.data[parser_state->pos] >= '0' &&
            parser_state->bytes.data[parser_state->pos] <= '9'))
      parser_state->pos++;
    if (begin == parser_state->pos) parser_state->bad = true;
  }
  if (parser_state->pos < parser_state->bytes.size &&
      (parser_state->bytes.data[parser_state->pos] == 'e' ||
       parser_state->bytes.data[parser_state->pos] == 'E')) {
    parser_state->pos++;
    if (parser_state->pos < parser_state->bytes.size &&
        (parser_state->bytes.data[parser_state->pos] == '+' ||
         parser_state->bytes.data[parser_state->pos] == '-'))
      parser_state->pos++;
    size_t begin = parser_state->pos;
    while (sn_work_on(parser_state->meter, 1),
           (parser_state->pos < parser_state->bytes.size &&
            parser_state->bytes.data[parser_state->pos] >= '0' &&
            parser_state->bytes.data[parser_state->pos] <= '9'))
      parser_state->pos++;
    if (begin == parser_state->pos) parser_state->bad = true;
  }
  sn_string number_text =
      sn_text(parser_state->arena, parser_state->bytes.data + start,
              parser_state->pos - start);
  node->kind = SN_JNUMBER;
  if (!number_text.data) {
    parser_state->bad = true;
    return node;
  }

  node->string = number_text;
  return node;
}
/* JSON text may carry a byte-order mark or use a wide Unicode encoding.
 * Detect it from the leading bytes before walking the grammar. */
static bool decode_unicode(sn_arena* arena, sn_bytes* result) {
  sn_work_meter* meter = sn_meter;
  sn_bytes bytes = *result;
  size_t stride = 1, skip = 0;
  bool little = false;
  if (bytes.size >= 4 && bytes.data[0] == 0 && bytes.data[1] == 0 &&
      bytes.data[2] == 0xfe && bytes.data[3] == 0xff) {
    stride = 4;
    skip = 4;
  } else if (bytes.size >= 4 && bytes.data[0] == 0xff &&
             bytes.data[1] == 0xfe && bytes.data[2] == 0 &&
             bytes.data[3] == 0) {
    stride = 4;
    skip = 4;
    little = true;
  } else if (bytes.size >= 2 && bytes.data[0] == 0xfe &&
             bytes.data[1] == 0xff) {
    stride = 2;
    skip = 2;
  } else if (bytes.size >= 2 && bytes.data[0] == 0xff &&
             bytes.data[1] == 0xfe) {
    stride = 2;
    skip = 2;
    little = true;
  } else if (bytes.size >= 3 && bytes.data[0] == 0xef &&
             bytes.data[1] == 0xbb && bytes.data[2] == 0xbf)
    skip = 3;
  else if (bytes.size >= 4) {
    if (!bytes.data[0] && !bytes.data[1] && !bytes.data[2])
      stride = 4;
    else if (!bytes.data[1] && !bytes.data[2] && !bytes.data[3]) {
      stride = 4;
      little = true;
    } else if (!bytes.data[0] && !bytes.data[2])
      stride = 2;
    else if (!bytes.data[1] && !bytes.data[3]) {
      stride = 2;
      little = true;
    }
  }
  if (stride == 1) {
    if (skip) {
      result->data += skip;
      result->size -= skip;
    }
    return true;
  }
  if ((bytes.size - skip) % stride || bytes.size > SIZE_MAX / 3) return false;
  uint8_t* out = sn_alloc(arena, bytes.size, 3);
  if (!out) return false;
  size_t written = 0;
  uint32_t high = 0;
  for (size_t index = skip; sn_work_on(meter, 1), (index < bytes.size);
       index += stride) {
    uint32_t scalar = 0;
    for (size_t node = 0; sn_work_on(meter, 1), (node < stride); node++)
      scalar = (scalar << 8) |
               bytes.data[index + (little ? stride - 1 - node : node)];
    if (stride == 2 && scalar >= 0xd800 && scalar <= 0xdbff) {
      if (high) return false;
      high = scalar;
      continue;
    }
    if (high) {
      if (scalar < 0xdc00 || scalar > 0xdfff) return false;
      scalar = 0x10000 + ((high - 0xd800) << 10) + scalar - 0xdc00;
      high = 0;
    }
    if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
      return false;
    if (scalar < 0x80)
      out[written++] = (uint8_t)scalar;
    else if (scalar < 0x800) {
      out[written++] = (uint8_t)(0xc0 | (scalar >> 6));
      out[written++] = (uint8_t)(0x80 | (scalar & 63));
    } else if (scalar < 0x10000) {
      out[written++] = (uint8_t)(0xe0 | (scalar >> 12));
      out[written++] = (uint8_t)(0x80 | ((scalar >> 6) & 63));
      out[written++] = (uint8_t)(0x80 | (scalar & 63));
    } else {
      out[written++] = (uint8_t)(0xf0 | (scalar >> 18));
      out[written++] = (uint8_t)(0x80 | ((scalar >> 12) & 63));
      out[written++] = (uint8_t)(0x80 | ((scalar >> 6) & 63));
      out[written++] = (uint8_t)(0x80 | (scalar & 63));
    }
  }
  if (high) return false;
  *result = (sn_bytes){out, written};
  return true;
}

/* Decode supported Unicode encodings and require one complete JSON value with
 * no trailing nonspace bytes. */
sn_json* sn_json_read(sn_arena* arena, sn_bytes bytes) {
  if (!decode_unicode(arena, &bytes)) return nullptr;
  parser parser_state = {arena, sn_meter, bytes, 0, false};
  sn_json* node = read_json_value(&parser_state, 0);
  skip_space(&parser_state);
  return parser_state.bad || parser_state.pos != bytes.size || arena->failed
             ? nullptr
             : node;
}

/* Return the first matching object member by bytewise key; nonobjects and null
 * nodes have no fields. */
const sn_json* sn_json_get(const sn_json* object, const char* key) {
  if (object && object->kind == SN_JOBJECT)
    for (const sn_json* child = object->child; sn_work(1), (child);
         child = child->next)
      if (sn_equal(child->key, key)) return child;
  return nullptr;
}

/* Treat missing/null fields as absent, but reject a present field of the wrong
 * type or a nonfinite numeric value. */
static const sn_json* typed_field(const sn_json* node, const char* key,
                                  sn_json_kind kind, bool* ok) {
  const sn_json* child = sn_json_get(node, key);
  if (!child || child->kind == SN_JNULL) return nullptr;
  if (child->kind != kind) {
    *ok = false;
    return nullptr;
  }
  return child;
}

static double number_field(const sn_json* child, bool* ok) {
  double value = 0;
  if (!child || !sn_double(child->string, &value) || !isfinite(value))
    *ok = false;
  return value;
}

/* Read optional string content while preserving the shared typed-field failure
 * flag. */
static sn_string text_field(sn_arena* arena, const sn_json* node,
                            const char* key, bool* ok) {
  const sn_json* child = typed_field(node, key, SN_JSTRING, ok);
  return child ? sn_text(arena, (const uint8_t*)child->string.data,
                         child->string.length)
               : (sn_string){0};
}

/* Prefer exact integer spelling; decimal/exponent tokens must represent an
 * integral in-range binary64 value. */
static int64_t int_field(const sn_json* node, const char* key, int64_t fallback,
                         bool* ok) {
  const sn_json* child = typed_field(node, key, SN_JNUMBER, ok);
  if (!child) return fallback;
  sn_work(child->string.length);
  sn_work(child->string.length);
  sn_work(child->string.length);
  if (!memchr(child->string.data, '.', child->string.length) &&
      !memchr(child->string.data, 'e', child->string.length) &&
      !memchr(child->string.data, 'E', child->string.length)) {
    int64_t exact;
    if (sn_integer(child->string, &exact)) return exact;
    *ok = false;
    return fallback;
  }
  double number = number_field(child, ok);
  if (number < -9223372036854775808.0 || number >= 9223372036854775808.0 ||
      trunc(number) != number) {
    *ok = false;
    return fallback;
  }
  return (int64_t)number;
}

/* Read an optional boolean with the caller fallback; wrong types invalidate the
 * containing model. */
static bool bool_field(const sn_json* node, const char* key, bool fallback,
                       bool* ok) {
  const sn_json* child = typed_field(node, key, SN_JBOOL, ok);
  return child ? child->boolean : fallback;
}

/* Materialize absent required display text as an owned empty string. */
static sn_string text_or_empty(sn_arena* arena, sn_string text) {
  return text.data ? text : sn_text(arena, (const uint8_t*)"", 0);
}

/* Decode Base64 or the quote-hash JSON variant. Only hashes immediately
 * following a quote become colons. */
static void layer_table(sn_arena* arena, sn_arena* scratch, sn_string text,
                        sn_page* page) {
  if (!sn_unset(text).data) return;
  sn_bytes bytes = {(const uint8_t*)text.data, text.length}, decoded;
  sn_work(text.length);
  sn_work(text.length);
  if (text.data[0] == '[' &&
      (strstr(text.data, "\"#") || strstr(text.data, "\":"))) {
    uint8_t* copy = sn_alloc(scratch, bytes.size, 1);
    if (!copy) return;
    sn_work_memcpy(copy, bytes.data, bytes.size);
    for (size_t index = 1; sn_work(1), (index < bytes.size); index++)
      if (copy[index] == '#' && copy[index - 1] == '"') copy[index] = ':';
    bytes.data = copy;
  } else if (sn_base64(scratch, bytes, false, &decoded))
    bytes = decoded;
  sn_string repaired = sn_text(scratch, bytes.data, bytes.size);
  bytes = (sn_bytes){(const uint8_t*)repaired.data, repaired.length};
  sn_json* root = sn_json_read(scratch, bytes);
  bool ok = root && root->kind == SN_JARRAY;
  if (!ok) goto fail;
  sn_layer_info* rows = sn_alloc(arena, root->count, sizeof *rows);
  if (!rows) return;
  size_t index = 0;
  for (const sn_json* node = root->child; sn_work(1), (node);
       node = node->next) {
    if (node->kind != SN_JOBJECT) {
      ok = false;
      break;
    }
    rows[index++] = (sn_layer_info){
        .layer_id = int_field(node, "layerId", 0, &ok),
        .name = text_or_empty(arena, text_field(arena, node, "name", &ok)),
        .is_background = bool_field(node, "isBackgroundLayer", false, &ok),
        .is_visible = bool_field(node, "isVisible", true, &ok),
        .is_deleted = bool_field(node, "isDeleted", false, &ok),
        .is_current = bool_field(node, "isCurrentLayer", false, &ok)};
  }
  if (ok) {
    page->layer_info = rows;
    page->layer_info_count = index;
    return;
  }

fail:
  sn_log_writef(SN_LOG_ERROR, "layer table does not read");
}

/* Require all four numeric coordinates, converting corner pairs to extents
 * where requested. */
static sn_rect read_box(const sn_json* node, const char* const keys[4],
                        bool* ok, bool corners) {
  double values[4] = {0};
  if (!node || node->kind != SN_JOBJECT) {
    *ok = false;
    return (sn_rect){.is_null = true};
  }
  for (size_t index = 0; sn_work(1), (index < 4); index++) {
    const sn_json* child = typed_field(node, keys[index], SN_JNUMBER, ok);
    if (!child)
      *ok = false;
    else
      values[index] = number_field(child, ok);
  }
  double width = corners ? values[2] - values[0] : values[2];
  double height = corners ? values[3] - values[1] : values[3];
  if (!isfinite(width) || !isfinite(height)) *ok = false;
  return (sn_rect){values[0], values[1], width, height, false};
}

/* Decode a stored recognition document and join Text labels with newlines,
 * checking space for both separators and the terminator. */
static const sn_recognition* recognition_read(sn_arena* arena,
                                              sn_arena* scratch,
                                              sn_bytes bytes) {
  sn_bytes decoded;
  if (!sn_base64(scratch, bytes, true, &decoded)) goto fail;
  sn_json* root = sn_json_read(scratch, decoded);
  if (!root || root->kind != SN_JOBJECT) goto fail;
  bool ok = true;
  const sn_json* elements = typed_field(root, "elements", SN_JARRAY, &ok);
  sn_recognition* recognition = sn_alloc(arena, 1, sizeof *recognition);
  if (!recognition) return nullptr;
  size_t count = elements ? elements->count : 0;
  sn_element* entries = sn_alloc(arena, count, sizeof *entries);
  if (!entries) return nullptr;
  size_t index = 0, text_size = 0, texts = 0;
  for (const sn_json* node = elements ? elements->child : nullptr;
       sn_work(1), (node); node = node->next) {
    if (node->kind != SN_JOBJECT) {
      ok = false;
      break;
    }
    sn_element* element = entries + index++;
    element->kind = text_or_empty(arena, text_field(arena, node, "type", &ok));
    element->label = text_field(arena, node, "label", &ok);
    const sn_json* words = typed_field(node, "words", SN_JARRAY, &ok);
    size_t written = words ? words->count : 0;
    sn_word* words_out = sn_alloc(arena, written, sizeof *words_out);
    if (!words_out) return nullptr;
    for (const sn_json* word_node = words ? words->child : nullptr;
         sn_work(1), (word_node); word_node = word_node->next) {
      if (word_node->kind != SN_JOBJECT) {
        ok = false;
        break;
      }
      sn_word* dest = words_out + element->word_count++;
      dest->label =
          text_or_empty(arena, text_field(arena, word_node, "label", &ok));
      dest->box.is_null = true;
      const sn_json* bb =
          typed_field(word_node, "bounding-box", SN_JOBJECT, &ok);
      if (bb) {
        const char* keys[] = {"x", "y", "width", "height"};
        dest->box = read_box(bb, keys, &ok, false);
      }
    }
    element->words = words_out;
    if (sn_equal(element->kind, "Text") && element->label.data) {
      if (text_size == SIZE_MAX ||
          element->label.length >= SIZE_MAX - text_size - 1) {
        arena->failed = true;
        return nullptr;
      }
      text_size += element->label.length + 1;
      texts++;
    }
  }
  if (!ok) goto fail;
  char* text = sn_alloc(arena, text_size + 1, 1);
  if (!text) return nullptr;
  size_t pos = 0, seen = 0;
  for (size_t word_node = 0; sn_work(1), (word_node < index); word_node++)
    if (sn_equal(entries[word_node].kind, "Text") &&
        entries[word_node].label.data) {
      if (seen++) text[pos++] = '\n';
      sn_work_memcpy(text + pos, entries[word_node].label.data,
                     entries[word_node].label.length);
      pos += entries[word_node].label.length;
    }
  (void)texts;
  recognition->text = (sn_string){text, pos};
  recognition->elements = entries;
  recognition->element_count = index;
  return recognition;
fail:
  sn_log_writef(SN_LOG_ERROR, "recognition block does not read");
  return nullptr;
}

/* Decode an optional rectangle array; any malformed rectangle invalidates the
 * containing highlight document. */
static bool read_rectangles(sn_arena* arena, const sn_json* node,
                            const char* name, const char* const keys[4],
                            const sn_rect** out, size_t* count, bool* ok) {
  const sn_json* list = typed_field(node, name, SN_JARRAY, ok);
  if (!list) return true;
  sn_rect* recognition = sn_alloc(arena, list->count, sizeof *recognition);
  if (!recognition) return false;
  for (const sn_json* child = list->child; sn_work(1), (child);
       child = child->next)
    recognition[(*count)++] = read_box(child, keys, ok, true);
  *out = recognition;
  return true;
}

/* Group byte-identical keys with file position as a deterministic tie-break. */
static int compare_highlight_keys(const void* x, const void* y) {
  sn_work(1);
  const sn_json* first = *(const sn_json* const*)x;
  const sn_json* second = *(const sn_json* const*)y;
  size_t common = first->key.length < second->key.length ? first->key.length
                                                         : second->key.length;
  int comparison = sn_work_memcmp(first->key.data, second->key.data, common);
  if (comparison) return comparison;
  if (first->key.length != second->key.length)
    return (first->key.length > second->key.length) -
           (first->key.length < second->key.length);
  return (first->order > second->order) - (first->order < second->order);
}

/* Sort numeric page keys rather than their lexical spellings. */
static int compare_highlight_pages(const void* x, const void* y) {
  sn_work(1);
  const sn_json* first = *(const sn_json* const*)x;
  const sn_json* second = *(const sn_json* const*)y;
  int64_t first_page = 0, second_page = 0;
  (void)sn_integer(first->key, &first_page);
  (void)sn_integer(second->key, &second_page);
  if (first_page != second_page)
    return (first_page > second_page) - (first_page < second_page);
  return (first->order > second->order) - (first->order < second->order);
}

/* Decode document highlights in numeric page order, converting zero-based pages
 * and millisecond timestamps. */
static void highlights_read(sn_arena* arena, sn_arena* scratch, sn_bytes bytes,
                            sn_notebook* notebook) {
  sn_bytes decoded;
  if (!sn_base64(scratch, bytes, true, &decoded)) goto fail;
  sn_json* root = sn_json_read(scratch, decoded);
  if (!root || root->kind != SN_JOBJECT) goto fail;
  const sn_json** pages = sn_alloc(scratch, root->count, sizeof *pages);
  if (!pages) return;
  size_t count = 0, page_count = 0;
  for (const sn_json* entry = root->child; sn_work(1), (entry);
       entry = entry->next)
    pages[page_count++] = entry;
  sn_work_sort(pages, page_count, sizeof *pages, compare_highlight_keys);
  size_t unique = 0;
  for (size_t index = 0; sn_work(1), (index < page_count); index++) {
    const sn_json* entry = pages[index];
    if (unique && pages[unique - 1]->key.length == entry->key.length &&
        !sn_work_memcmp(pages[unique - 1]->key.data, entry->key.data,
                        entry->key.length))
      continue;
    if (entry->kind != SN_JARRAY || entry->count > SIZE_MAX - count) goto fail;
    count += entry->count;
    pages[unique++] = entry;
  }
  page_count = unique;
  sn_work_sort(pages, page_count, sizeof *pages, compare_highlight_pages);
  sn_highlight* highlights = sn_alloc(arena, count, sizeof *highlights);
  if (!highlights) return;
  bool ok = true;
  size_t used = 0;
  for (size_t index = 0; sn_work(1), (index < page_count); index++) {
    int64_t page = 0;
    (void)sn_integer(pages[index]->key, &page);
    for (const sn_json* node = pages[index]->child; sn_work(1), (node);
         node = node->next) {
      if (node->kind != SN_JOBJECT) goto fail;
      sn_highlight* highlight = highlights + used++;
      int64_t page_entry = int_field(node, "page", page, &ok);
      if (page_entry == INT64_MAX) goto fail;
      highlight->page_number = page_entry + 1;
      highlight->kind = int_field(node, "annotationType", 0, &ok);
      highlight->color_type = int_field(node, "colorType", 0, &ok);
      highlight->comment = text_field(arena, node, "annotationContent", &ok);
      if (!highlight->comment.length) highlight->comment = (sn_string){0};
      const sn_json* date = typed_field(node, "time", SN_JNUMBER, &ok);
      if (date) {
        highlight->has_date = true;
        highlight->date = number_field(date, &ok) / 1000;
      }
      const char* screen[] = {"left", "top", "right", "bottom"};
      const char* doc[] = {"x0", "y0", "x1", "y1"};
      if (!read_rectangles(arena, node, "rnRectList", screen, &highlight->rects,
                           &highlight->rect_count, &ok) ||
          !read_rectangles(arena, node, "mupdfRectList", doc,
                           &highlight->document_rects,
                           &highlight->document_rect_count, &ok))
        return;
    }
  }
  if (ok) {
    notebook->highlights = highlights;
    notebook->highlight_count = used;
    return;
  }

fail:
  sn_log_writef(SN_LOG_ERROR, "highlight block does not read");
}

void sn_layer_table(sn_arena* arena, sn_string text, sn_page* page) {
  sn_arena scratch = {.budget = arena->budget};
  layer_table(arena, &scratch, text, page);
  sn_scratch_free(arena, &scratch);
}

const sn_recognition* sn_recognition_read(sn_arena* arena, sn_bytes bytes) {
  /* Standalone decoder inputs have no file offset and are deliberately not
   * cached. */
  if (!arena->file_base) {
    sn_arena scratch = {.budget = arena->budget};
    const sn_recognition* result = recognition_read(arena, &scratch, bytes);
    sn_scratch_free(arena, &scratch);
    return result;
  }
  const sn_recognition** cached =
      sn_cached(arena, sn_offset(arena, bytes.data), SN_CACHE_RECOGNITION, 0);
  if (cached) return *cached;
  sn_decode_begin(arena, sn_offset(arena, bytes.data), SN_CACHE_RECOGNITION, 0);
  sn_arena scratch = {.budget = arena->budget};
  const sn_recognition* result = recognition_read(arena, &scratch, bytes);
  sn_scratch_free(arena, &scratch);
  sn_cache_put(arena, sn_offset(arena, bytes.data), SN_CACHE_RECOGNITION, 0,
               &result, sizeof result);
  return result;
}

void sn_highlights(sn_arena* arena, sn_bytes bytes, sn_notebook* notebook) {
  sn_arena scratch = {.budget = arena->budget};
  highlights_read(arena, &scratch, bytes, notebook);
  sn_scratch_free(arena, &scratch);
}
