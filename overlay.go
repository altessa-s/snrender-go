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
	"image"
	"io"
	"runtime"
	"unsafe"
)

// HasAnnotations reports whether a mark sheet or highlight names the zero-based
// source document page. It does not need source bytes or check their page count;
// PageCount instead counts the mark's stored sheets.
func (n *Notebook) HasAnnotations(page int) (bool, error) {
	p, err := n.acquire()
	if err != nil {
		return false, err
	}
	defer n.release()
	if page < 0 {
		return false, invalid("page out of range")
	}
	var found C.bool
	var e C.snr_error
	if status := C.snr_mark_page_has_annotations(p, C.size_t(page), &found, &e); status != C.SNR_OK {
		return false, cError(status, &e)
	}
	return bool(found), nil
}

func (n *Notebook) overlay(ctx context.Context, pdf []byte, page, count, kind int, o *OverlayOptions, w io.Writer) (*renderResult, error) {
	if ctx == nil {
		return nil, invalid("nil context")
	}
	p, err := n.acquire()
	if err != nil {
		return nil, err
	}
	defer n.release()
	if len(pdf) == 0 {
		return nil, invalid("empty source PDF")
	}
	// These indexes address source pages, not the mark's sparse sheet array.
	if page < 0 {
		return nil, invalid("page or first out of range")
	}
	if kind == 2 && count != AllPages && count <= 0 {
		return nil, invalid("count out of range")
	}
	request, err := o.request()
	if err != nil {
		return nil, err
	}
	var progress func(int, int)
	if o != nil {
		progress = o.Render.Progress
	}
	c := newCall(ctx, w, progress)
	defer c.finish()
	if o != nil && o.Render.Title != "" {
		request.render.title = C.CString(o.Render.Title)
		defer C.free(unsafe.Pointer(request.render.title))
	}
	var buffer C.snr_buffer
	var bitmap C.snr_bitmap
	var report C.snr_overlay_result
	defer C.snr_buffer_free(&buffer)
	defer C.snr_bitmap_free(&bitmap)
	if o != nil && o.Result != nil {
		defer func() { *o.Result = goOverlayResult(report) }()
	}
	var e C.snr_error
	status := C.snrgo_overlay(p, (*C.uint8_t)(unsafe.Pointer(&pdf[0])), C.size_t(len(pdf)), C.size_t(page), C.size_t(count), C.int(kind), &request, C.uintptr_t(c.handle), C.bool(w != nil), &buffer, &bitmap, &report, &e)
	runtime.KeepAlive(pdf)
	if err := c.resultError(status, &e); err != nil {
		return nil, err
	}
	r := &renderResult{}
	if kind == 0 {
		data, err := copyBytes(unsafe.Pointer(bitmap.pixels), uint64(bitmap.stride)*uint64(bitmap.height))
		if err != nil {
			return nil, err
		}
		r.bitmap = &image.RGBA{Pix: data, Stride: int(bitmap.stride), Rect: image.Rect(0, 0, int(bitmap.width), int(bitmap.height))}
	} else if w == nil {
		r.bytes, err = copyBytes(unsafe.Pointer(buffer.bytes), uint64(buffer.size))
		if err != nil {
			return nil, err
		}
	}
	return r, nil
}

// OverlayBitmap draws the source PDF and mark as Go-owned premultiplied RGBA.
// Page is a zero-based source index; dimensions are ceil(rotated CropBox points
// * Render.Scale). Keep pdf immutable until return, including in callbacks.
// Cairo previews use PDFium; see SetPDFiumLock when sharing it with other code.
func (n *Notebook) OverlayBitmap(ctx context.Context, pdf []byte, page int, o *OverlayOptions) (*image.RGBA, error) {
	r, err := n.overlay(ctx, pdf, page, 1, 0, o, nil)
	if err != nil {
		return nil, err
	}
	return r.bitmap, nil
}

// OverlayPNG encodes OverlayBitmap as PNG. Keep pdf immutable until return.
func (n *Notebook) OverlayPNG(ctx context.Context, pdf []byte, page int, o *OverlayOptions) ([]byte, error) {
	r, err := n.overlay(ctx, pdf, page, 1, 1, o, nil)
	if err != nil {
		return nil, err
	}
	return r.bytes, nil
}

// WriteOverlayPNG writes OverlayPNG to w. Discard partial output on error.
// Callbacks use the same helper and panic/Goexit cleanup rules as WritePNG.
// Keep pdf immutable until return.
func (n *Notebook) WriteOverlayPNG(ctx context.Context, w io.Writer, pdf []byte, page int, o *OverlayOptions) error {
	if w == nil {
		return invalid("nil writer")
	}
	_, err := n.overlay(ctx, pdf, page, 1, 1, o, w)
	return err
}

// OverlayPDF preserves source page objects and adds transparent raster ink.
// First is an inclusive source index; count is positive or AllPages. Whole
// document exports append an incremental update; ranges rewrite selected pages.
// It does not use PDFium. Keep pdf immutable until return.
func (n *Notebook) OverlayPDF(ctx context.Context, pdf []byte, first, count int, o *OverlayOptions) ([]byte, error) {
	r, err := n.overlay(ctx, pdf, first, count, 2, o, nil)
	if err != nil {
		return nil, err
	}
	return r.bytes, nil
}

// WriteOverlayPDF writes OverlayPDF to w. Discard partial output on error.
// Callbacks use the same helper and panic/Goexit cleanup rules as WritePDF.
// Keep pdf immutable until return.
func (n *Notebook) WriteOverlayPDF(ctx context.Context, w io.Writer, pdf []byte, first, count int, o *OverlayOptions) error {
	if w == nil {
		return invalid("nil writer")
	}
	_, err := n.overlay(ctx, pdf, first, count, 2, o, w)
	return err
}
