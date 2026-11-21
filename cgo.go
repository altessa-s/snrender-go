// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

// Apple overlays use CoreGraphics. All Cairo builds enable upstream's
// SNR_PDFIUM option, including darwin with snrender_cairo. As in rmrender-go,
// callers supply the PDFium SDK include/library paths and runtime rpath through
// CGO_CFLAGS and CGO_LDFLAGS. Windows' SDK import library is pdfium.dll.lib,
// found with -lpdfium.dll (cgo rejects the -l: spelling). Ship pdfium.dll at
// runtime. See README for the pinned checksum-verified SDK fetcher.

/*
#cgo CFLAGS: -DSNR_SPD -std=c2x -fvisibility=hidden -include ${SRCDIR}/clib_prelude.h
#cgo CFLAGS: -I${SRCDIR}/internal/clib/snrender/include -I${SRCDIR}/internal/clib/snrender/src
#cgo CFLAGS: -I${SRCDIR}/internal/clib/snnote/include -I${SRCDIR}/internal/clib/snnote/src
#cgo CFLAGS: -I${SRCDIR}/internal/clib/snspd/include -I${SRCDIR}/internal/clib/snspd/src -I${SRCDIR}/internal/clib/snspd/sqlite
#cgo darwin,!snrender_cairo CFLAGS: -fobjc-arc
#cgo LDFLAGS: -lz
#cgo darwin,!snrender_cairo LDFLAGS: -framework CoreGraphics -framework CoreText -framework CoreFoundation -framework ImageIO -framework PDFKit
#cgo linux windows darwin,snrender_cairo CFLAGS: -DSNR_CAIRO_BACKEND -DSNR_PDFIUM
#cgo linux windows darwin,snrender_cairo pkg-config: cairo cairo-ft freetype2 gdk-pixbuf-2.0 zlib
#cgo linux darwin,snrender_cairo LDFLAGS: -lpdfium
#cgo windows LDFLAGS: -lpdfium.dll
#cgo darwin linux LDFLAGS: -lm
#include "bridge.h"
#include "clib_digest.h"
*/
import "C"

// VersionInfo identifies the binding and the exact C implementations it embeds.
type VersionInfo struct{ Binding, Library, RendererCommit, ReaderCommit, DrawingReaderCommit string }

// Version reports the runtime library version and immutable source identities.
func Version() VersionInfo {
	return VersionInfo{"1.0.0", C.GoString(C.snr_version()), C.SNRGO_SNRENDER_COMMIT, C.SNRGO_SNNOTE_COMMIT, C.SNRGO_SNSPD_COMMIT}
}
