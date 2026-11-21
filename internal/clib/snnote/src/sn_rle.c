/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"

constexpr uint8_t RLE_BLOCK_MARKER = 255;
constexpr uint8_t RLE_HELD_BIT = 128;
constexpr uint8_t RLE_LENGTH_MASK = 127;
constexpr unsigned RLE_HELD_SHIFT = 7;
constexpr size_t RLE_PAIR_SIZE = 2;

/* Test remaining room before advancing: the length belongs to the input,
 * and adding it first would let an overflow pass the extent check. */
static bool write_run(uint8_t* output, size_t total, size_t* cursor,
                      uint8_t code, size_t length) {
  if (length > total - *cursor) return false;
  if (output) sn_work_memset(output + *cursor, code, length);
  *cursor += length;
  return true;
}

/* Null output measures the stream. An odd last byte has no length and is
 * ignored. The result distinguishes undershoot from overshoot because only
 * overshoot permits the old blank-template block-size fallback. */
sn_rle_result sn_rle(sn_bytes bytes, size_t total, size_t block,
                     uint8_t* output) {
  size_t cursor = 0;
  bool held = false;
  uint8_t held_code = 0, held_length = 0;
  for (size_t index = 0; sn_work(2), (bytes.size - index >= RLE_PAIR_SIZE);
       index += RLE_PAIR_SIZE) {
    uint8_t code = bytes.data[index], length = bytes.data[index + 1];
    if (held) {
      held = false;
      size_t run = ((size_t)(held_length & RLE_LENGTH_MASK) + 1)
                   << RLE_HELD_SHIFT;
      if (code == held_code) {
        if (!write_run(output, total, &cursor, code, run + 1 + length))
          return SN_RLE_OVERSHOOT;
        continue;
      }
      if (!write_run(output, total, &cursor, held_code, run))
        return SN_RLE_OVERSHOOT;
    }
    if (length == RLE_BLOCK_MARKER) {
      if (!write_run(output, total, &cursor, code, block))
        return SN_RLE_OVERSHOOT;
    } else if (length & RLE_HELD_BIT) {
      held = true;
      held_code = code;
      held_length = length;
    } else if (!write_run(output, total, &cursor, code, (size_t)length + 1)) {
      return SN_RLE_OVERSHOOT;
    }
  }
  /* A final held run has no low byte. The writer truncates its high part
   * to the largest power-of-two expansion that fits the remaining pixels. */
  if (held) {
    for (int shift = RLE_HELD_SHIFT; sn_work(1), (shift >= 0); shift--) {
      size_t run = ((size_t)(held_length & RLE_LENGTH_MASK) + 1)
                   << (unsigned)shift;
      if (run <= total - cursor) {
        (void)write_run(output, total, &cursor, held_code, run);
        break;
      }
    }
  }
  return cursor == total ? SN_RLE_COMPLETE : SN_RLE_UNDERSHOOT;
}

/* Sheet inference uses the ordinary block size only; a permissive fallback
 * here would allow the same bytes to identify more than one sheet. */
bool sn_fits(sn_bytes bytes, size_t total) {
  return sn_rle(bytes, total, SN_RLE_BLOCK_SIZE, nullptr) == SN_RLE_COMPLETE;
}
