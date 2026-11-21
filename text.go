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
	"runtime"
	"unsafe"
)

// Rect is a rectangle in PDF points, relative to the top-left of the displayed
// rotated CropBox intersected with MediaBox. X increases right, Y down.
// Null rectangles select nothing; negative sizes and nonfinite values are invalid.
type Rect struct {
	X, Y, Width, Height float64
	IsNull              bool
}

// TextOptions bounds source PDF text extraction. Nil and zero fields select
// library defaults. Limits uses only pages, work, document work and output bytes.
// MaximumOutputBytes includes the C result's trailing NUL.
type TextOptions struct {
	Limits                                                   Limits
	MaximumSourceBytes, MaximumRectangles, MaximumCharacters int
}

// DefaultTextOptions reads independent defaults from the linked C library.
func DefaultTextOptions() TextOptions {
	c := C.snr_pdf_text_options_default()
	return TextOptions{Limits: goLimits(c.limits), MaximumSourceBytes: int(c.maximum_source_bytes), MaximumRectangles: int(c.maximum_rectangles), MaximumCharacters: int(c.maximum_characters)}
}

func (o *TextOptions) request() (C.snr_pdf_text_options, error) {
	c := C.snr_pdf_text_options_default()
	if o == nil {
		return c, nil
	}
	render, err := (&Options{Limits: o.Limits}).request()
	if err != nil {
		return c, err
	}
	c.limits = render.limits
	for _, field := range []struct {
		value  int
		target *C.size_t
		name   string
	}{
		{o.MaximumSourceBytes, &c.maximum_source_bytes, "MaximumSourceBytes"},
		{o.MaximumRectangles, &c.maximum_rectangles, "MaximumRectangles"},
		{o.MaximumCharacters, &c.maximum_characters, "MaximumCharacters"},
	} {
		value, err := sizeLimit(field.value, *field.target, field.name)
		if err != nil {
			return c, err
		}
		*field.target = value
	}
	return c, nil
}

// PageText extracts the union of rects from a zero-based source PDF page.
// Each character is selected once by its box centre (left/top inclusive,
// right/bottom exclusive), in engine reading order. Empty unions return "".
// Whitespace is collapsed and invisible characters removed; no OCR is performed.
// Keep pdf and options immutable until return. PDFKit/PDFium may differ in
// ligatures, soft hyphens and column order. Cairo shares SetPDFiumLock with previews.
func PageText(ctx context.Context, pdf []byte, page int, rects []Rect, o *TextOptions) (string, error) {
	if ctx == nil {
		return "", invalid("nil context")
	}
	if len(pdf) == 0 {
		return "", invalid("empty source PDF")
	}
	if page < 0 {
		return "", invalid("page out of range")
	}
	request, err := o.request()
	if err != nil {
		return "", err
	}
	// Reject the count before allocating the temporary rectangle array.
	if uint64(len(rects)) > uint64(request.maximum_rectangles) {
		return "", &Error{Status: StatusLimit, Message: "rectangle limit exceeded"}
	}
	cr := make([]C.sn_rect, len(rects))
	for i, r := range rects {
		cr[i] = C.sn_rect{x: C.double(r.X), y: C.double(r.Y), width: C.double(r.Width), height: C.double(r.Height), is_null: C.bool(r.IsNull)}
	}
	c := newCall(ctx, nil, nil)
	defer c.finish()
	var buffer C.snr_buffer
	defer C.snr_buffer_free(&buffer)
	var e C.snr_error
	status := C.snrgo_page_text((*C.uint8_t)(unsafe.Pointer(&pdf[0])), C.size_t(len(pdf)), C.size_t(page), (*C.sn_rect)(unsafe.Pointer(unsafe.SliceData(cr))), C.size_t(len(cr)), &request, C.uintptr_t(c.handle), &buffer, &e)
	runtime.KeepAlive(pdf)
	runtime.KeepAlive(cr)
	if err := c.resultError(status, &e); err != nil {
		return "", err
	}
	data, err := copyBytes(unsafe.Pointer(buffer.bytes), uint64(buffer.size))
	return string(data), err
}
