// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

/*
#include "bridge.h"
*/
import "C"
import (
	"context"
	"fmt"
	"image"
	"io"
	"runtime"
	"unsafe"
)

// DrawingParseError reports an embedded snspd reader failure. Status uses the
// shared ParseStatus codes (not snspd's numeric ABI); Value is the signed SQLite
// result code, zero for non-engine failures. It matches the reader sentinels
// and common renderer resource/unsupported sentinels just like ParseError.
type DrawingParseError struct {
	Status  ParseStatus
	Value   int64
	Message string
}

func (e *DrawingParseError) Error() string {
	if e.Message == "" {
		return fmt.Sprintf("snrender: drawing parse: %s", e.Status)
	}
	return fmt.Sprintf("snrender: drawing parse: %s: %s", e.Status, e.Message)
}

// Is matches drawing parse status or the existing reader and renderer sentinels.
func (e *DrawingParseError) Is(target error) bool {
	if t, ok := target.(*DrawingParseError); ok {
		return e.Status == t.Status
	}
	return (&ParseError{Status: e.Status}).Is(target)
}

func drawingParseError(status C.spd_status, e *C.spd_error) error {
	// snspd has no image-encoding statuses; its limit code differs from snnote's.
	s := ParseStatus(status)
	if status == C.SPD_ERR_LIMIT {
		s = ParseLimit
	}
	return &DrawingParseError{Status: s, Value: int64(e.value), Message: C.GoString(&e.message[0])}
}

func drawing(ctx context.Context, data []byte, kind int, o *DrawingOptions, w io.Writer) (*renderResult, error) {
	if ctx == nil {
		return nil, invalid("nil context")
	}
	request, parse, err := o.request()
	if err != nil {
		return nil, err
	}
	c := newCall(ctx, w, nil)
	defer c.finish()
	// Poll before the synchronous, independently bounded parse. There is no
	// cancellation hook inside spd_open; rendering polls again immediately after it.
	if snrgoCancel(C.uintptr_t(c.handle)) {
		return nil, &Error{Status: StatusCancelled, Cause: c.contextErr}
	}
	var p *C.spd_drawing
	var pe C.spd_error
	size := len(data)
	if size == 0 {
		data = []byte{0}
	}
	status := C.spd_open(unsafe.Pointer(unsafe.SliceData(data)), C.size_t(size), &parse, &p, &pe)
	runtime.KeepAlive(data)
	if status != C.SPD_OK {
		return nil, drawingParseError(status, &pe)
	}
	defer C.spd_drawing_free(p)
	var buffer C.snr_buffer
	var bitmap C.snr_bitmap
	defer C.snr_buffer_free(&buffer)
	defer C.snr_bitmap_free(&bitmap)
	var e C.snr_error
	result := C.snrgo_drawing(p, C.int(kind), &request, C.uintptr_t(c.handle), C.bool(w != nil), &buffer, &bitmap, &e)
	if err := c.resultError(result, &e); err != nil {
		return nil, err
	}
	r := &renderResult{}
	if kind == 0 {
		pixels, err := copyBytes(unsafe.Pointer(bitmap.pixels), uint64(bitmap.stride)*uint64(bitmap.height))
		if err != nil {
			return nil, err
		}
		r.bitmap = &image.RGBA{Pix: pixels, Stride: int(bitmap.stride), Rect: image.Rect(0, 0, int(bitmap.width), int(bitmap.height))}
	} else if w == nil {
		r.bytes, err = copyBytes(unsafe.Pointer(buffer.bytes), uint64(buffer.size))
		if err != nil {
			return nil, err
		}
	}
	return r, nil
}

// DrawingBitmap parses .spd bytes and renders Go-owned premultiplied RGBA.
// Parsing copies data and is synchronous; cancellation applies before parsing
// and throughout rendering. No model from another Go module is accepted.
func DrawingBitmap(ctx context.Context, data []byte, o *DrawingOptions) (*image.RGBA, error) {
	r, err := drawing(ctx, data, 0, o, nil)
	if err != nil {
		return nil, err
	}
	return r.bitmap, nil
}

// DrawingPNG parses .spd bytes and renders a PNG, using painted bounds by default.
func DrawingPNG(ctx context.Context, data []byte, o *DrawingOptions) ([]byte, error) {
	r, err := drawing(ctx, data, 1, o, nil)
	if err != nil {
		return nil, err
	}
	return r.bytes, nil
}

// DrawingPDF parses .spd bytes and renders one raster PDF page. Physical size
// uses page pixels * 72 / canvas PPI, clamped to at least 3 points per side.
func DrawingPDF(ctx context.Context, data []byte, o *DrawingOptions) ([]byte, error) {
	r, err := drawing(ctx, data, 2, o, nil)
	if err != nil {
		return nil, err
	}
	return r.bytes, nil
}

// DrawingSVG parses .spd bytes and embeds a lossless PNG in a standalone SVG.
func DrawingSVG(ctx context.Context, data []byte, o *DrawingOptions) ([]byte, error) {
	r, err := drawing(ctx, data, 3, o, nil)
	if err != nil {
		return nil, err
	}
	return r.bytes, nil
}

// WriteDrawingPNG writes DrawingPNG to w. Discard partial output on error.
// Writer panic/Goexit propagates on the caller after C cleanup, as in WritePNG.
func WriteDrawingPNG(ctx context.Context, w io.Writer, data []byte, o *DrawingOptions) error {
	if w == nil {
		return invalid("nil writer")
	}
	_, err := drawing(ctx, data, 1, o, w)
	return err
}

// WriteDrawingPDF writes DrawingPDF to w. Discard partial output on error.
// Writer panic/Goexit propagates on the caller after C cleanup, as in WritePDF.
func WriteDrawingPDF(ctx context.Context, w io.Writer, data []byte, o *DrawingOptions) error {
	if w == nil {
		return invalid("nil writer")
	}
	_, err := drawing(ctx, data, 2, o, w)
	return err
}

// WriteDrawingSVG writes DrawingSVG to w. Discard partial output on error.
// Writer panic/Goexit propagates on the caller after C cleanup, as in WriteSVG.
func WriteDrawingSVG(ctx context.Context, w io.Writer, data []byte, o *DrawingOptions) error {
	if w == nil {
		return invalid("nil writer")
	}
	_, err := drawing(ctx, data, 3, o, w)
	return err
}
