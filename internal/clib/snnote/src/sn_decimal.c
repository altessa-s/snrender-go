/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <fenv.h>
#include <float.h>

#include "sn_internal.h"

/* 800 significant decimal digits distinguish every binary64 rounding
 * boundary, including half the smallest subnormal. Extra digits need only
 * a sticky bit. 128 limbs hold the coefficient, denominator and normalization
 * shifts (fewer than 4096 bits); no input-sized allocation is needed. */
constexpr size_t DECIMAL_DIGITS = 800;
constexpr size_t DECIMAL_LIMBS = 128;
static_assert(DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
              "decimal conversion requires binary64");
typedef struct decimal_integer {
  uint32_t limbs[DECIMAL_LIMBS];
  size_t used;
} decimal_integer;

#ifdef SNNOTE_TESTING
static _Thread_local size_t decimal_operations;
size_t sn_test_decimal_operations(void) { return decimal_operations; }
void sn_test_decimal_reset(void) { decimal_operations = 0; }
#endif
static int bit_length(const decimal_integer* value) {
  if (!value->used) return 0;
  uint32_t top = value->limbs[value->used - 1];
  int bits = (int)(value->used - 1) * 32;
  while (sn_work(1), (top)) {
    bits++;
    top >>= 1;
  }
  return bits;
}
/* Shift a normalized magnitude in one limb pass, including whole limbs. */
static void shift_integer(decimal_integer* value, int bits) {
#ifdef SNNOTE_TESTING
  decimal_operations++;
#endif
  size_t words = (size_t)bits / 32;
  unsigned remainder = (unsigned)bits % 32;
  uint64_t carry = 0;
  decimal_integer shifted = {0};
  for (size_t index = 0; sn_work(1), (index < value->used); index++) {
    uint64_t limb = ((uint64_t)value->limbs[index] << remainder) | carry;
    shifted.limbs[index + words] = (uint32_t)limb;
    carry = limb >> 32;
  }
  shifted.used = value->used + words;
  if (carry) shifted.limbs[shifted.used++] = (uint32_t)carry;
  *value = shifted;
}

/* Multiply by a small radix and add a digit. The caller's decimal bounds
 * ensure that the carry always has a spare limb. */
static void multiply_add(decimal_integer* integer, uint32_t radix,
                         uint32_t digit) {
  uint64_t carry = digit;
  for (size_t index = 0; sn_work(1), (index < integer->used); index++) {
    uint64_t product = (uint64_t)integer->limbs[index] * radix + carry;
    integer->limbs[index] = (uint32_t)product;
    carry = product >> 32;
  }
  if (carry) integer->limbs[integer->used++] = (uint32_t)carry;
}

/* Unsigned magnitude order; leading zero limbs have already been removed. */
static int compare_integer(const decimal_integer* first,
                           const decimal_integer* second) {
#ifdef SNNOTE_TESTING
  decimal_operations++;
#endif
  if (first->used != second->used) return first->used > second->used ? 1 : -1;
  for (size_t index = first->used; sn_work(1), (index > 0); index--) {
    if (first->limbs[index - 1] != second->limbs[index - 1])
      return first->limbs[index - 1] > second->limbs[index - 1] ? 1 : -1;
  }
  return 0;
}

/* Subtract only after comparison establishes a nonnegative difference. */
static void subtract_integer(decimal_integer* first,
                             const decimal_integer* second) {
  uint64_t borrow = 0;
  for (size_t index = 0; sn_work(1), (index < first->used); index++) {
    uint64_t subtrahend =
        borrow + (index < second->used ? second->limbs[index] : 0);
    uint64_t original = first->limbs[index];
    first->limbs[index] = (uint32_t)(original - subtrahend);
    borrow = original < subtrahend;
  }
  while (sn_work(1), (first->used && !first->limbs[first->used - 1]))
    first->used--;
}

/* Parse ASCII decimal syntax without consulting or modifying process locale.
 * Integer division supplies the significand bits and an exact remainder,
 * then one nearest-even rounding decides the binary64 value. */
static bool decimal_read(sn_string text, double* out, bool fast) {
  /* Bound the signed counters well beyond any addressable notebook. The
   * explicit exponent can saturate only beyond all possible cancellation. */
  if (!text.data || !text.length || !out) return false;
#if SIZE_MAX > INT64_MAX / 4
  if (text.length > INT64_MAX / 4) return false;
#endif
  size_t position = 0;
  bool negative = false;
  if (text.data[position] == '-' || text.data[position] == '+') {
    negative = text.data[position++] == '-';
    if (position == text.length) return false;
  }
  decimal_integer numerator = {0}, denominator = {.limbs = {1}, .used = 1};
  size_t significant = 0;
  uint64_t coefficient = 0;
  int64_t decimal_exponent = 0;
  bool point = false, digit_seen = false, sticky = false;
  for (; sn_work(1), (position < text.length); position++) {
    char character = text.data[position];
    if (character == '.' && !point) {
      point = true;
      continue;
    }
    if (character < '0' || character > '9') break;
    digit_seen = true;
    uint32_t digit = (uint32_t)(character - '0');
    if (point) decimal_exponent--;
    if (!significant && !digit) continue;
    if (significant < DECIMAL_DIGITS) {
      if (significant < 19) coefficient = coefficient * 10 + digit;
      multiply_add(&numerator, 10, digit);
      significant++;
    } else {
      decimal_exponent++;
      sticky |= digit != 0;
    }
  }
  if (!digit_seen) return false;
  if (position < text.length &&
      (text.data[position] == 'e' || text.data[position] == 'E')) {
    position++;
    bool exponent_negative = false;
    if (position < text.length &&
        (text.data[position] == '+' || text.data[position] == '-'))
      exponent_negative = text.data[position++] == '-';
    size_t start = position;
    int64_t exponent = 0;
    for (; sn_work(1), (position < text.length); position++) {
      char character = text.data[position];
      if (character < '0' || character > '9') return false;
      int digit = character - '0';
      if (exponent > (INT64_MAX / 2 - digit) / 10)
        exponent = INT64_MAX / 2;
      else
        exponent = exponent * 10 + digit;
    }
    if (position == start) return false;
    decimal_exponent += exponent_negative ? -exponent : exponent;
  }
  if (position != text.length) return false;
  /* Both operands are exact binary64 values. One correctly rounded operation
   * therefore gives the same result as exact rational rounding. */
  static const double powers[] = {
      1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
      1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
  if (FLT_EVAL_METHOD == 0 && fast && fegetround() == FE_TONEAREST &&
      significant <= 19 && (uint64_t)(double)coefficient == coefficient &&
      decimal_exponent >= -22 && decimal_exponent <= 22) {
    double value = decimal_exponent < 0
                       ? (double)coefficient / powers[-decimal_exponent]
                       : (double)coefficient * powers[decimal_exponent];
    *out = negative ? -value : value;
    return true;
  }
  int64_t magnitude = decimal_exponent + (int64_t)significant;
  if (!significant || magnitude < -324) {
    *out = negative ? -0.0 : 0.0;
    return true;
  }
  if (magnitude > 309) {
    *out = negative ? -INFINITY : INFINITY;
    return true;
  }
  for (; sn_work(1), (decimal_exponent > 0); decimal_exponent--)
    multiply_add(&numerator, 10, 0);
  for (; sn_work(1), (decimal_exponent < 0); decimal_exponent++)
    multiply_add(&denominator, 10, 0);
  int binary_exponent = bit_length(&numerator) - bit_length(&denominator);
  if (binary_exponent >= 0)
    shift_integer(&denominator, binary_exponent);
  else
    shift_integer(&numerator, -binary_exponent);
  /* Equal bit lengths put the ratio in [1/2,2); at most one correction. */
  if (compare_integer(&numerator, &denominator) < 0) {
    shift_integer(&numerator, 1);
    binary_exponent--;
  }
  /* Subnormals have fewer significant bits; rounding at their fixed binary
   * scale avoids double rounding at the normal/subnormal boundary. */
  int bits = binary_exponent < -1022 ? binary_exponent + 1075 : 53;
  uint64_t significand = 0;
  if (bits < 0) {
    *out = negative ? -0.0 : 0.0;
    return true;
  }
  for (int bit = 0; sn_work(1), (bit < bits); bit++) {
    significand <<= 1;
    if (compare_integer(&numerator, &denominator) >= 0) {
      subtract_integer(&numerator, &denominator);
      significand |= 1;
    }
    multiply_add(&numerator, 2, 0);
  }
  /* With zero bits, the normalized value itself is the rounding bit. */
  int halfway = compare_integer(&numerator, &denominator);
  if (halfway > 0 || (halfway == 0 && (sticky || (significand & 1))))
    significand++;
  double value = ldexp((double)significand, binary_exponent - bits + 1);
  *out = negative ? -value : value;
  return true;
}

bool sn_double(sn_string text, double* out) {
  return decimal_read(text, out, true);
}
#ifdef SNNOTE_TESTING
bool sn_test_double_fallback(sn_string text, double* out) {
  return decimal_read(text, out, false);
}
#endif
