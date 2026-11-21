// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

/*
#include "bridge.h"
*/
import "C"
import (
	"runtime/cgo"
	"sync"
)

// SetPDFiumLock installs a shared process-lifetime mutex for Cairo previews and text.
// Call once before any preview or PageText call. Nil, late and repeated installation fail with
// ErrInvalidArgument; Apple returns ErrUnsupported. No installation is needed
// when this package is the only PDFium user: C already serializes its own use.
//
// When sharing libpdfium with rmrender-go or other code, guard every external
// PDFium operation with this same mutex, including initialization and cleanup.
// Keep PDFium initialized while either binding can still be used.
// Do not hold it while calling a preview or PageText. External calls must not call back
// into previews or PageText while holding it. PDF exports do not use PDFium.
// The concrete mutex excludes caller code from the C lock callbacks; never
// copy it. A successful installation retains it for the process lifetime.
func SetPDFiumLock(mu *sync.Mutex) error {
	if mu == nil {
		return invalid("nil PDFium mutex")
	}
	h := cgo.NewHandle(mu)
	var e C.snr_error
	status := C.snrgo_pdfium_set_lock(C.uintptr_t(h), &e)
	if status != C.SNR_OK {
		h.Delete()
		return cError(status, &e)
	}
	return nil
}

//export snrgoPDFiumLock
func snrgoPDFiumLock(handle C.uintptr_t) {
	cgo.Handle(handle).Value().(*sync.Mutex).Lock()
}

//export snrgoPDFiumUnlock
func snrgoPDFiumUnlock(handle C.uintptr_t) {
	cgo.Handle(handle).Value().(*sync.Mutex).Unlock()
}
