/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef SNNOTE_RASTER_H
#define SNNOTE_RASTER_H
#include "snnote/types.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Raster operations refuse more than 16,777,216 pixels, including products
 * that overflow size_t. Dimensions for bitmap decoding must be positive.
 * PNG and JPEG return SN_ERR_IMAGE_ENCODING; other protocols return
 * SN_ERR_UNSUPPORTED_ENCODING. The library does not encode images.
 * Outputs are unchanged on failure. Each result has independent ownership. */
constexpr size_t SN_MAX_RASTER_PIXELS = 16777216;
static_assert(SN_MAX_RASTER_PIXELS <= SIZE_MAX / 2);
typedef struct sn_raster {
  /* Pixel width and height; both positive for decoded bitmaps. */
  size_t width, height;
  /* Owned row-major intensity plane, width * height bytes; top row first. */
  const uint8_t* gray;
  /* Owned matching coverage plane, 0 transparent through 255 opaque. */
  const uint8_t* alpha;
} sn_raster;
/* Decode RATTA_RLE codes into an independently owned buffer. bitmap, out and
 * size are required; err may be null. Returns SN_OK, SN_ERR_INVALID_ARGUMENT
 * for null pointers or zero dimensions, SN_ERR_LIMIT for excessive pixel count,
 * SN_ERR_IMAGE_ENCODING for PNG/JPEG, SN_ERR_UNSUPPORTED_ENCODING for unknown
 * protocols, SN_ERR_MALFORMED for coverage mismatch, SN_ERR_OUT_OF_MEMORY for
 * allocation failure. *out is unchanged on failure; err is set on every call.
 */
[[nodiscard]] SN_API sn_status sn_bitmap_codes(const sn_bitmap* bitmap,
                                               size_t width, size_t height,
                                               uint8_t** out, size_t* size,
                                               sn_error* err);
/* Release a result of sn_bitmap_codes, as a single buffer; null is ignored. */
SN_API void sn_codes_free(uint8_t* codes);
/* Decode one bitmap through palette, or the current palette when null.
 * Same required arguments, statuses, error and failure-output guarantees as
 * sn_bitmap_codes. Success transfers the raster and both planes to the caller.
 */
[[nodiscard]] SN_API sn_status sn_bitmap_raster(const sn_bitmap* bitmap,
                                                size_t width, size_t height,
                                                const sn_palette* palette,
                                                sn_raster** out, sn_error* err);
/* Release the raster and both planes together; null is ignored. */
SN_API void sn_raster_free(sn_raster* raster);
#ifdef __cplusplus
}
#endif
#endif
