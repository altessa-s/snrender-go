/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
/* Claim bytes only after checking remaining room; a failed reader remains
 * failed. */
sn_bytes sn_take(sn_reader* reader, size_t count) {
  if (reader->bad || count > reader->size - reader->pos) {
    reader->bad = true;
    return (sn_bytes){0};
  }
  sn_work(count);
  sn_bytes bytes = {reader->data + reader->pos, count};
  reader->pos += count;
  return bytes;
}

/* Read a little-endian word without alignment assumptions; truncated input
 * marks the reader bad. */
uint32_t sn_u32(sn_reader* reader) {
  sn_bytes bytes = sn_take(reader, 4);
  if (reader->bad) return 0;
  return (uint32_t)bytes.data[0] | (uint32_t)bytes.data[1] << 8 |
         (uint32_t)bytes.data[2] << 16 | (uint32_t)bytes.data[3] << 24;
}

/* Read a little-endian halfword without alignment assumptions; truncated input
 * marks the reader bad. */
uint16_t sn_u16(sn_reader* reader) {
  sn_bytes bytes = sn_take(reader, 2);
  if (reader->bad) return 0;
  return (uint16_t)((uint16_t)bytes.data[0] | (uint16_t)bytes.data[1] << 8);
}

/* Copy the stored IEEE bit pattern into a float; geometry callers decide
 * whether nonfinite values are meaningful. */
float sn_f32(sn_reader* reader) {
  uint32_t bits = sn_u32(reader);
  float value;
  static_assert(sizeof value == sizeof bits);
  sn_work_memcpy(&value, &bits, 4);
  return value;
}

/* Read an array count and validate its product by division before any caller
 * multiplies by stride. */
size_t sn_count(sn_reader* reader, size_t stride) {
  size_t count = sn_u32(reader);
  if (reader->bad ||
      (stride && count > (reader->size - reader->pos) / stride)) {
    reader->bad = true;
    return 0;
  }
  return count;
}

/* Resolve an absolute length-prefixed block. Address zero denotes absence;
 * negative and out-of-file addresses fail. */
bool sn_block(sn_bytes file, int64_t address, sn_bytes* out) {
  if (!address) {
    *out = (sn_bytes){0};
    return true;
  }
  if (address < 0 || (uint64_t)address > file.size) return false;
  sn_reader reader = {file.data, file.size, (size_t)address, false};
  size_t count = sn_u32(&reader);
  if (reader.bad || count > reader.size - reader.pos) return false;
  *out = (sn_bytes){reader.data + reader.pos, count};
  return true;
}

/* Distinguish a missing length field from a complete length that claims bytes
 * beyond the file. */
sn_status sn_block_status(sn_bytes file, int64_t address) {
  if (!address) return SN_OK;
  if (address < 0) return SN_ERR_MALFORMED;
  if ((uint64_t)address > file.size || file.size - (size_t)address < 4)
    return SN_ERR_UNEXPECTED_EOF;
  sn_reader reader = {file.data, file.size, (size_t)address, false};
  size_t length = sn_u32(&reader);
  return length > reader.size - reader.pos ? SN_ERR_MALFORMED : SN_OK;
}
