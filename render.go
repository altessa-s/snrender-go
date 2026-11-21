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
	"unsafe"
)

// AllPages selects every page from the inclusive starting index in PDF calls.
const AllPages = -1

type renderResult struct {
	bytes  []byte
	bitmap *image.RGBA
	raster *Raster
}

func (n *Notebook) render(ctx context.Context, page, count, kind int, o *Options, w io.Writer) (*renderResult, error) {
	if ctx == nil {
		return nil, invalid("nil context")
	}
	p, err := n.acquire()
	if err != nil {
		return nil, err
	}
	defer n.release()
	argument := "page"
	if kind == 5 {
		argument = "first"
	}
	if page < 0 || uint64(page) >= uint64(p.page_count) {
		return nil, invalid(argument + " out of range")
	}
	if kind == 5 && count != AllPages && (count <= 0 || uint64(count) > uint64(p.page_count)-uint64(page)) {
		return nil, invalid("count out of range")
	}
	request, err := o.request()
	if err != nil {
		return nil, err
	}
	var progress func(int, int)
	if o != nil {
		progress = o.Progress
	}
	c := newCall(ctx, w, progress)
	defer c.finish()
	if o != nil && o.Title != "" {
		request.title = C.CString(o.Title)
		defer C.free(unsafe.Pointer(request.title))
	}
	var buffer C.snr_buffer
	var bitmap C.snr_bitmap
	var raster C.snr_raster
	// These defers run before finish can propagate a callback panic or Goexit.
	defer C.snr_buffer_free(&buffer)
	defer C.snr_bitmap_free(&bitmap)
	defer C.snr_raster_free(&raster)
	var e C.snr_error
	status := C.snrgo_render(p, C.size_t(page), C.size_t(count), C.int(kind), &request, C.uintptr_t(c.handle), C.bool(w != nil && kind >= 2), &buffer, &bitmap, &raster, &e)
	if err := c.resultError(status, &e); err != nil {
		return nil, err
	}
	result := &renderResult{}
	switch kind {
	case 0:
		data, err := copyBytes(unsafe.Pointer(bitmap.pixels), uint64(bitmap.stride)*uint64(bitmap.height))
		if err != nil {
			return nil, err
		}
		result.bitmap = &image.RGBA{Pix: data, Stride: int(bitmap.stride), Rect: image.Rect(0, 0, int(bitmap.width), int(bitmap.height))}
	case 1:
		gray, err := copyBytes(unsafe.Pointer(raster.gray), uint64(raster.width)*uint64(raster.height))
		if err != nil {
			return nil, err
		}
		alpha, err := copyBytes(unsafe.Pointer(raster.alpha), uint64(raster.width)*uint64(raster.height))
		if err != nil {
			return nil, err
		}
		result.raster = &Raster{Rect: image.Rect(0, 0, int(raster.width), int(raster.height)), Gray: gray, Alpha: alpha}
	default:
		if w == nil {
			result.bytes, err = copyBytes(unsafe.Pointer(buffer.bytes), uint64(buffer.size))
			if err != nil {
				return nil, err
			}
		}
	}
	return result, nil
}

// Bitmap returns Go-owned premultiplied RGBA pixels, top row first.
func (n *Notebook) Bitmap(ctx context.Context, page int, o *Options) (*image.RGBA, error) {
	r, err := n.render(ctx, page, 1, 0, o, nil)
	if err != nil {
		return nil, err
	}
	return r.bitmap, nil
}

// Raster owns separate straight gray and alpha planes. Transparent gray is 255.
type Raster struct {
	Rect        image.Rectangle
	Gray, Alpha []uint8
}

// GrayImage shares the gray plane, so edits are visible in both values.
func (r *Raster) GrayImage() *image.Gray {
	return &image.Gray{Pix: r.Gray, Stride: r.Rect.Dx(), Rect: r.Rect}
}

// AlphaImage shares the alpha plane, so edits are visible in both values.
func (r *Raster) AlphaImage() *image.Alpha {
	return &image.Alpha{Pix: r.Alpha, Stride: r.Rect.Dx(), Rect: r.Rect}
}

// Raster returns Go-owned straight gray and alpha planes.
func (n *Notebook) Raster(ctx context.Context, page int, o *Options) (*Raster, error) {
	r, err := n.render(ctx, page, 1, 1, o, nil)
	if err != nil {
		return nil, err
	}
	return r.raster, nil
}

func (n *Notebook) encoded(ctx context.Context, page, count, kind int, o *Options) ([]byte, error) {
	r, err := n.render(ctx, page, count, kind, o, nil)
	if err != nil {
		return nil, err
	}
	return r.bytes, nil
}

// PNG encodes a page as RGBA PNG.
func (n *Notebook) PNG(ctx context.Context, page int, o *Options) ([]byte, error) {
	return n.encoded(ctx, page, 1, 2, o)
}

// WritePNG writes the bytes of PNG to w. Discard partial output on error.
// The writer runs on a helper goroutine and may start independent calls.
// Panic and Goexit propagate on the caller after C resources are released.
func (n *Notebook) WritePNG(ctx context.Context, w io.Writer, page int, o *Options) error {
	if w == nil {
		return invalid("nil writer")
	}
	_, err := n.render(ctx, page, 1, 2, o, w)
	return err
}

// SVG embeds a lossless page image in a standalone SVG.
func (n *Notebook) SVG(ctx context.Context, page int, o *Options) ([]byte, error) {
	return n.encoded(ctx, page, 1, 3, o)
}

// WriteSVG writes the bytes of SVG to w. Discard partial output on error.
// The writer runs on a helper goroutine and may start independent calls.
// Panic and Goexit propagate on the caller after C resources are released.
func (n *Notebook) WriteSVG(ctx context.Context, w io.Writer, page int, o *Options) error {
	if w == nil {
		return invalid("nil writer")
	}
	_, err := n.render(ctx, page, 1, 3, o, w)
	return err
}

// PagePDF exports one page with links and optional invisible recognition.
func (n *Notebook) PagePDF(ctx context.Context, page int, o *Options) ([]byte, error) {
	return n.encoded(ctx, page, 1, 4, o)
}

// WritePagePDF writes the bytes of PagePDF to w. Discard partial output on error.
// The writer runs on a helper goroutine and may start independent calls.
// Panic and Goexit propagate on the caller after C resources are released.
func (n *Notebook) WritePagePDF(ctx context.Context, w io.Writer, page int, o *Options) error {
	if w == nil {
		return invalid("nil writer")
	}
	_, err := n.render(ctx, page, 1, 4, o, w)
	return err
}

// PDF exports a range; first is inclusive, count is positive or AllPages.
func (n *Notebook) PDF(ctx context.Context, first, count int, o *Options) ([]byte, error) {
	return n.encoded(ctx, first, count, 5, o)
}

// WritePDF writes the bytes of PDF to w. Discard partial output on error.
// The writer runs on a helper goroutine and may start independent calls.
// Panic and Goexit propagate on the caller after C resources are released.
func (n *Notebook) WritePDF(ctx context.Context, w io.Writer, first, count int, o *Options) error {
	if w == nil {
		return invalid("nil writer")
	}
	_, err := n.render(ctx, first, count, 5, o, w)
	return err
}
