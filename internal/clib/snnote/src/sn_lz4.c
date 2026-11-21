/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
/* Consume all 255 extension bytes and the terminating extension, checking
 * before each addition. */
static bool read_length(sn_bytes bytes, size_t* pos, size_t* length) {
  uint8_t extension;
  do {
    if (*pos == bytes.size) return false;
    extension = bytes.data[(*pos)++];
    if (*length > SIZE_MAX - extension) return false;
    *length += extension;
  } while (sn_work(1), (extension == 255));
  return true;
}

/* Expand a raw block within its declared extent and ratio limit. Copy matches
 * bytewise because distance-one references overlap their output. */
bool sn_lz4(sn_arena* arena, sn_bytes bytes, size_t expanded, sn_bytes* out) {
  sn_work(bytes.size);
  size_t base = bytes.size ? bytes.size : 1;
  if (base <= SIZE_MAX / 255 && expanded > base * 255) return false;
  if (!sn_declared_fits(arena, expanded, 1)) return false;
  uint8_t* output = sn_alloc(arena, expanded, 1);
  if (!output) return false;
  if (!expanded) {
    *out = (sn_bytes){output, 0};
    return true;
  }
  size_t input_position = 0, output_position = 0;
  while (sn_work(1), (input_position < bytes.size)) {
    uint8_t token = bytes.data[input_position++];
    size_t literal = token >> 4;
    if (literal == 15 && !read_length(bytes, &input_position, &literal))
      return false;
    if (literal > bytes.size - input_position ||
        literal > expanded - output_position)
      return false;
    sn_work_memcpy(output + output_position, bytes.data + input_position,
                   literal);
    input_position += literal;
    output_position += literal;
    if (input_position == bytes.size) break;
    if (bytes.size - input_position < 2) return false;
    size_t distance = (size_t)bytes.data[input_position] |
                      (size_t)bytes.data[input_position + 1] << 8;
    input_position += 2;
    if (!distance || distance > output_position) return false;
    size_t match = token & 15;
    if (match == 15 && !read_length(bytes, &input_position, &match))
      return false;
    if (match > SIZE_MAX - 4) return false;
    match += 4;
    if (match > expanded - output_position) return false;
    for (size_t field_index = 0; sn_work(1), (field_index < match);
         field_index++) {
      output[output_position] = output[output_position - distance];
      output_position++;
    }
  }
  if (output_position != expanded) return false;
  *out = (sn_bytes){output, expanded};
  return true;
}
