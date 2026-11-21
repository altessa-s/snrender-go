/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef SNNOTE_PARSER_H
#define SNNOTE_PARSER_H
#include "snnote/types.h"
#ifdef __cplusplus
extern "C" {
#endif

/* On failure the output pointer is unchanged and all temporary allocations
 * are released. Error is optional. Null data and output pointers are invalid.
 * JSON nesting is limited to 64 containers. LZ4 expanded lengths must not
 * exceed 255 times the larger of one and the compressed byte count.
 * Stroke tail candidate counts must be below 1,048,576. */
/* Total live parse heap storage (owner, arena chunks, headers and scratch) is
 * limited to 1 MiB plus 128 times the input byte count. Budget exhaustion
 * by accumulation returns SN_ERR_LIMIT, releases storage and leaves *out
 * unchanged. A declaration exceeding the whole budget on its own is malformed
 * optional content and follows local recovery instead. */
constexpr size_t SN_PARSE_BUDGET_FLOOR = 1048576;
constexpr size_t SN_PARSE_BUDGET_MULTIPLE = 128;
/* Parse work is limited separately from live storage: 1 MiB plus 256
 * units per input byte. Units cover examined bytes and traversal operations;
 * exhaustion returns SN_ERR_LIMIT with a work-budget diagnostic. */
constexpr size_t SN_PARSE_WORK_FLOOR = 1048576;
constexpr size_t SN_PARSE_WORK_MULTIPLE = 256;
/* Longer protocol names are retained as unknown encodings, never interned. */
constexpr size_t SN_PROTOCOL_MAX_LENGTH = 32;
constexpr size_t SN_JSON_MAX_DEPTH = 64;
static_assert(SN_JSON_MAX_DEPTH == 64);
/* Parse a complete in-memory container, copying its bytes into the result.
 * SN_OK transfers ownership to the caller. SN_ERR_INVALID_ARGUMENT rejects
 * null data/out; SN_ERR_UNSUPPORTED_FORMAT rejects signature/version syntax;
 * SN_ERR_UNEXPECTED_EOF reports missing required bytes; SN_ERR_MALFORMED
 * reports bad footer/header blocks or an unidentifiable sheet;
 * SN_ERR_OUT_OF_MEMORY reports any failed allocation. Optional content may
 * be dropped with diagnostics; skipped pages/strokes are counted.
 * On every failure *out is unchanged. err may be null; otherwise every call
 * assigns status, format location/count and a content-free message. */
[[nodiscard]] SN_API sn_status sn_parse_notebook(const uint8_t* data,
                                                 size_t size, sn_notebook** out,
                                                 sn_error* err);
/* Release the notebook and every borrowed pointer into it. Null is ignored. */
SN_API void sn_notebook_free(sn_notebook* notebook);
#ifdef __cplusplus
}
#endif
#endif
