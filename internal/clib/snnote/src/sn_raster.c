/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "sn_internal.h"
/* Reject empty or excessive extents before multiplying caller-supplied
 * dimensions. */
static sn_status pixel_count_checked(size_t width, size_t height,
                                     size_t* count) {
  if (!width || !height) return SN_ERR_INVALID_ARGUMENT;
  if (width > SN_MAX_RASTER_PIXELS / height) return SN_ERR_LIMIT;
  *count = width * height;
  return SN_OK;
}

/* Allocate both planes with the descriptor. Callers first enforce the raster
 * ceiling, bounding both multiplication and header addition. */
static sn_raster* allocate_raster(size_t width, size_t height,
                                  size_t pixel_count) {
  sn_raster* result = malloc(sizeof *result + pixel_count * 2);
  if (!result) return nullptr;
  uint8_t* pixels = (uint8_t*)(result + 1);
  *result = (sn_raster){width, height, pixels, pixels + pixel_count};
  return result;
}

/* Release one independently owned raster; null is accepted. */
void sn_raster_free(sn_raster* result) { free(result); }

/* Release one independently owned code buffer; null is accepted. */
void sn_codes_free(uint8_t* bitmap) { free(bitmap); }

/* Decode a single native bitmap, trying the shorter block only after overshoot.
 * Publish the owned output only after exact coverage. */
sn_status sn_bitmap_codes(const sn_bitmap* bitmap, size_t width, size_t height,
                          uint8_t** out, size_t* size, sn_error* err) {
  if (!bitmap || !out || !size || (!bitmap->bytes.data && bitmap->bytes.size))
    return sn_error_set(err, SN_ERR_INVALID_ARGUMENT, 0);
  if (bitmap->encoding != SN_ENCODING_RATTA_RLE)
    return sn_error_set(err,
                        bitmap->encoding == SN_ENCODING_PNG ||
                                bitmap->encoding == SN_ENCODING_JPEG
                            ? SN_ERR_IMAGE_ENCODING
                            : SN_ERR_UNSUPPORTED_ENCODING,
                        0);
  size_t pixel_count;
  sn_status status = pixel_count_checked(width, height, &pixel_count);
  if (status != SN_OK) return sn_error_set(err, status, 0);
  uint8_t* result = malloc(pixel_count);
  if (!result) return sn_error_set(err, SN_ERR_OUT_OF_MEMORY, 0);
  uint8_t* pixels = result;
  sn_rle_result decoded =
      sn_rle(bitmap->bytes, pixel_count, SN_RLE_BLOCK_SIZE, pixels);
  if (decoded == SN_RLE_OVERSHOOT) {
    decoded =
        sn_rle(bitmap->bytes, pixel_count, SN_RLE_BLANK_BLOCK_SIZE, pixels);
    if (!decoded)
      sn_log_writef(SN_LOG_DEBUG, "RLE blank-template run length used");
  }
  if (decoded) {
    free(result);
    return sn_error_set(err, SN_ERR_MALFORMED, 0);
  }
  *size = pixel_count;
  *out = result;
  return sn_error_set(err, SN_OK, 0);
}

/* Map each decoded code through the chosen palette into independent gray and
 * alpha planes; null palette selects current colors. */
sn_status sn_bitmap_raster(const sn_bitmap* bitmap, size_t width, size_t height,
                           const sn_palette* palette, sn_raster** out,
                           sn_error* err) {
  if (!out) return sn_error_set(err, SN_ERR_INVALID_ARGUMENT, 0);
  uint8_t* codes = nullptr;
  size_t count;
  sn_status status =
      sn_bitmap_codes(bitmap, width, height, &codes, &count, err);
  if (status != SN_OK) return status;
  sn_raster* result = allocate_raster(width, height, count);
  if (!result) {
    sn_codes_free(codes);
    return sn_error_set(err, SN_ERR_OUT_OF_MEMORY, 0);
  }
  sn_palette current;
  if (!palette) {
    current = sn_palette_current();
    palette = &current;
  }
  uint8_t* gray = (uint8_t*)(result + 1);
  uint8_t* alpha = gray + count;
  for (size_t index = 0; index < count; index++) {
    sn_ink ink = palette->inks[codes[index]];
    gray[index] = ink.gray;
    alpha[index] = ink.alpha;
  }
  sn_codes_free(codes);
  *out = result;
  return sn_error_set(err, SN_OK, 0);
}
