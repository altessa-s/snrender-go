// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

/*
#include "bridge.h"
*/
import "C"
import (
	"runtime"
	"sync"
	"sync/atomic"
	"unsafe"
)

// Notebook owns an immutable C copy. It must not be copied after first use.
// Concurrent renders share it; Close returns immediately, deferring storage release until active calls finish. No finalizer runs.
type Notebook struct {
	mu       sync.Mutex
	active   int
	closed   bool
	notebook *C.sn_notebook
}

// Open parses a complete .note or .mark, copying bytes into C-owned storage.
// Optional damaged content follows the reader's recovery rules.
func Open(data []byte) (*Notebook, error) {
	var p *C.sn_notebook
	var e C.sn_error
	// The reader copies synchronously and retains no pointer into this slice.
	size := len(data)
	if size == 0 {
		data = []byte{0}
	}
	status := C.sn_parse_notebook((*C.uint8_t)(unsafe.Pointer(&data[0])), C.size_t(size), &p, &e)
	runtime.KeepAlive(data)
	if status != C.SN_OK {
		return nil, &ParseError{Status: ParseStatus(status), Value: uint64(e.value), Message: C.GoString(&e.message[0])}
	}
	liveNotebooks.Add(1)
	return &Notebook{notebook: p}, nil
}

var liveNotebooks atomic.Int64

// Close prevents new calls and returns immediately. Active calls retain storage
// until the last call finishes. Close is idempotent, including on nil.
func (n *Notebook) Close() error {
	if n == nil {
		return nil
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	n.closed = true
	n.freeIdle()
	return nil
}

func (n *Notebook) freeIdle() {
	if n.closed && n.active == 0 && n.notebook != nil {
		C.sn_notebook_free(n.notebook)
		n.notebook = nil
		liveNotebooks.Add(-1)
	}
}

func (n *Notebook) acquire() (*C.sn_notebook, error) {
	if n == nil {
		return nil, ErrClosed
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.notebook == nil {
		return nil, ErrClosed
	}
	n.active++
	return n.notebook, nil
}

func (n *Notebook) release() {
	n.mu.Lock()
	defer n.mu.Unlock()
	n.active--
	n.freeIdle()
}

// PageCount reports readable pages; a closed notebook reports ErrClosed.
func (n *Notebook) PageCount() (int, error) {
	p, err := n.acquire()
	if err != nil {
		return 0, err
	}
	defer n.release()
	return int(p.page_count), nil
}

// Geometry describes stored pixels and physical PDF sheet size.
type Geometry struct{ Width, Height, DPI, WidthPoints, HeightPoints float64 }

// PageGeometry reports zero-based page dimensions, respecting device overrides.
func (n *Notebook) PageGeometry(page int) (Geometry, error) {
	p, err := n.acquire()
	if err != nil {
		return Geometry{}, err
	}
	defer n.release()
	if page < 0 || uint64(page) >= uint64(p.page_count) {
		return Geometry{}, invalid("page out of range")
	}
	var dpi C.double
	s := C.snrgo_geometry(p, C.size_t(page), &dpi)
	return Geometry{float64(s.width), float64(s.height), float64(dpi), float64(s.width * 72 / dpi), float64(s.height * 72 / dpi)}, nil
}
