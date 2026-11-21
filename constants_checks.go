// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

/*
#include "bridge.h"
#include <snnote/log.h>
*/
import "C"

// Both array lengths must be nonnegative, so ABI drift fails compilation.
var (
	_ [DocumentPDF - C.SNR_DOCUMENT_PDF]struct{}
	_ [C.SNR_DOCUMENT_PDF - DocumentPDF]struct{}
	_ [DocumentEPUB - C.SNR_DOCUMENT_EPUB]struct{}
	_ [C.SNR_DOCUMENT_EPUB - DocumentEPUB]struct{}
	_ [LogWarning - C.SNR_LOG_WARNING]struct{}
	_ [C.SNR_LOG_WARNING - LogWarning]struct{}
	_ [StatusOK - C.SNR_OK]struct{}
	_ [C.SNR_OK - StatusOK]struct{}
	_ [StatusInvalidArgument - C.SNR_ERR_INVALID_ARGUMENT]struct{}
	_ [C.SNR_ERR_INVALID_ARGUMENT - StatusInvalidArgument]struct{}
	_ [StatusOutOfMemory - C.SNR_ERR_OUT_OF_MEMORY]struct{}
	_ [C.SNR_ERR_OUT_OF_MEMORY - StatusOutOfMemory]struct{}
	_ [StatusLimit - C.SNR_ERR_LIMIT]struct{}
	_ [C.SNR_ERR_LIMIT - StatusLimit]struct{}
	_ [StatusUnsupported - C.SNR_ERR_UNSUPPORTED]struct{}
	_ [C.SNR_ERR_UNSUPPORTED - StatusUnsupported]struct{}
	_ [StatusBackend - C.SNR_ERR_BACKEND]struct{}
	_ [C.SNR_ERR_BACKEND - StatusBackend]struct{}
	_ [StatusCancelled - C.SNR_ERR_CANCELLED]struct{}
	_ [C.SNR_ERR_CANCELLED - StatusCancelled]struct{}
	_ [ParseOK - C.SN_OK]struct{}
	_ [C.SN_OK - ParseOK]struct{}
	_ [ParseInvalidArgument - C.SN_ERR_INVALID_ARGUMENT]struct{}
	_ [C.SN_ERR_INVALID_ARGUMENT - ParseInvalidArgument]struct{}
	_ [ParseOutOfMemory - C.SN_ERR_OUT_OF_MEMORY]struct{}
	_ [C.SN_ERR_OUT_OF_MEMORY - ParseOutOfMemory]struct{}
	_ [ParseUnsupportedFormat - C.SN_ERR_UNSUPPORTED_FORMAT]struct{}
	_ [C.SN_ERR_UNSUPPORTED_FORMAT - ParseUnsupportedFormat]struct{}
	_ [ParseUnexpectedEOF - C.SN_ERR_UNEXPECTED_EOF]struct{}
	_ [C.SN_ERR_UNEXPECTED_EOF - ParseUnexpectedEOF]struct{}
	_ [ParseMalformed - C.SN_ERR_MALFORMED]struct{}
	_ [C.SN_ERR_MALFORMED - ParseMalformed]struct{}
	_ [ParseImageEncoding - C.SN_ERR_IMAGE_ENCODING]struct{}
	_ [C.SN_ERR_IMAGE_ENCODING - ParseImageEncoding]struct{}
	_ [ParseUnsupportedEncoding - C.SN_ERR_UNSUPPORTED_ENCODING]struct{}
	_ [C.SN_ERR_UNSUPPORTED_ENCODING - ParseUnsupportedEncoding]struct{}
	_ [ParseLimit - C.SN_ERR_LIMIT]struct{}
	_ [C.SN_ERR_LIMIT - ParseLimit]struct{}
	_ [LogDebug - C.SNR_LOG_DEBUG]struct{}
	_ [C.SNR_LOG_DEBUG - LogDebug]struct{}
	_ [LogError - C.SNR_LOG_ERROR]struct{}
	_ [C.SNR_LOG_ERROR - LogError]struct{}
	_ [LogDebug - C.SN_LOG_DEBUG]struct{}
	_ [C.SN_LOG_DEBUG - LogDebug]struct{}
	_ [LogError - C.SN_LOG_ERROR]struct{}
	_ [C.SN_LOG_ERROR - LogError]struct{}
	_ [LogDebug - C.SPD_LOG_DEBUG]struct{}
	_ [C.SPD_LOG_DEBUG - LogDebug]struct{}
	_ [LogError - C.SPD_LOG_ERROR]struct{}
	_ [C.SPD_LOG_ERROR - LogError]struct{}
	_ [ParseOK - C.SPD_OK]struct{}
	_ [C.SPD_OK - ParseOK]struct{}
	_ [ParseInvalidArgument - C.SPD_ERR_INVALID_ARGUMENT]struct{}
	_ [C.SPD_ERR_INVALID_ARGUMENT - ParseInvalidArgument]struct{}
	_ [ParseOutOfMemory - C.SPD_ERR_OUT_OF_MEMORY]struct{}
	_ [C.SPD_ERR_OUT_OF_MEMORY - ParseOutOfMemory]struct{}
	_ [ParseUnsupportedFormat - C.SPD_ERR_UNSUPPORTED_FORMAT]struct{}
	_ [C.SPD_ERR_UNSUPPORTED_FORMAT - ParseUnsupportedFormat]struct{}
	_ [ParseUnexpectedEOF - C.SPD_ERR_UNEXPECTED_EOF]struct{}
	_ [C.SPD_ERR_UNEXPECTED_EOF - ParseUnexpectedEOF]struct{}
	_ [ParseMalformed - C.SPD_ERR_MALFORMED]struct{}
	_ [C.SPD_ERR_MALFORMED - ParseMalformed]struct{}
)
