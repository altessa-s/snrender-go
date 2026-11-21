/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
/* Map one alphabet byte to its sextet, or -1 for padding and nonalphabet bytes.
 */
static int base64_digit(uint8_t character) {
  if (character >= 'A' && character <= 'Z') return character - 'A';
  if (character >= 'a' && character <= 'z') return character - 'a' + 26;
  if (character >= '0' && character <= '9') return character - '0' + 52;
  if (character == '+') return 62;
  if (character == '/') return 63;
  return -1;
}

/* Decode complete quartets and observed excess padding. Stored blocks filter
 * only NUL, LF, CR and space; ordinary fields filter nothing. */
bool sn_base64(sn_arena* arena, sn_bytes input, bool stored, sn_bytes* out) {
  sn_work_meter* meter = sn_meter;
  size_t encoded_count = 0, padding = 0;
  for (size_t index = 0; sn_work_on(meter, 1), (index < input.size); index++) {
    uint8_t character = input.data[index];
    if (stored && (character == 0 || character == 10 || character == 13 ||
                   character == 32))
      continue;
    encoded_count++;
    if (character == '=')
      padding++;
    else if (padding || base64_digit(character) < 0)
      return false;
  }
  size_t digits = encoded_count - padding;
  size_t remainder = digits % 4;
  /* Padding is permitted beyond the final quartet. A quartet made only
   * of padding is accepted as a single zero byte. */
  if (remainder == 1 || (remainder && padding < 4 - remainder) ||
      (!digits && padding && padding < 4))
    return false;
  size_t size = digits / 4 * 3 + (remainder ? remainder - 1 : 0);
  if (!digits && padding) size = 1;
  uint8_t* output = sn_alloc(arena, size, 1);
  if (!output) return false;
  unsigned bits = 0, have = 0;
  size_t written = 0;
  for (size_t index = 0; sn_work_on(meter, 1), (index < input.size); index++) {
    int sextet = base64_digit(input.data[index]);
    if (sextet < 0) continue;
    bits = (bits << 6) | (unsigned)sextet;
    have += 6;
    if (have >= 8) {
      have -= 8;
      if (written < size) output[written++] = (uint8_t)(bits >> have);
    }
  }
  *out = (sn_bytes){output, size};
  return true;
}
