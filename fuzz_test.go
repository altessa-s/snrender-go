// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"bytes"
	"context"
	"encoding/xml"
	"errors"
	"github.com/altessa-s/snrender-go"
	"hash/fnv"
	"image/png"
	"math"
	"testing"
)

func boundedOptions() *snrender.Options {
	l := snrender.DefaultLimits()
	l.MaximumPixels = 81920
	l.MaximumDecodedPixels = 5000000
	l.MaximumPages = 4
	l.MaximumWork = 60000000
	l.MaximumDocumentWork = 240000000
	l.MaximumOutputBytes = 4 << 20
	return &snrender.Options{Scale: .125, Limits: l}
}
func FuzzRender(f *testing.F) {
	f.Add([]byte("not a notebook"))
	f.Fuzz(func(t *testing.T, data []byte) {
		if len(data) > 256<<10 {
			return
		}
		n, e := snrender.Open(data)
		if e != nil {
			var pe *snrender.ParseError
			if !errors.As(e, &pe) || pe.Status <= snrender.ParseOK || pe.Status > snrender.ParseLimit {
				t.Fatal(e)
			}
			return
		}
		defer n.Close()
		h := fnv.New32a()
		_, _ = h.Write(data)
		selector := h.Sum32()
		o := boundedOptions()
		o.StrictImages = selector&16 != 0
		o.DisableRecognition = selector&32 != 0
		o.HideBackground = selector&64 != 0
		count, e := n.PageCount()
		if e != nil {
			t.Fatal(e)
		}
		page := 0
		if count > 0 {
			page = int(selector>>8) % count
		}
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		if selector&128 != 0 {
			cancel()
		}
		switch selector >> 16 & 3 {
		case 1:
			o.Limits.MaximumPixels = 1024
		case 2:
			o.Limits.MaximumWork = 8000000
		case 3:
			o.Limits.MaximumOutputBytes = int(selector >> 20)
		}
		exerciseRender(ctx, t, n, page, count, o, selector)
	})
}

func documentedError(t *testing.T, err error) {
	t.Helper()
	var e *snrender.Error
	if !errors.As(err, &e) || e.Status < snrender.StatusInvalidArgument || e.Status > snrender.StatusCancelled {
		t.Fatalf("undocumented error: %v", err)
	}
}

func exerciseRender(ctx context.Context, t *testing.T, n *snrender.Notebook, page, count int, o *snrender.Options, selector uint32) {
	t.Helper()
	kind := selector % 10
	var data []byte
	var err error
	var writes int
	refused := errors.New("writer refusal")
	var out bytes.Buffer
	writer := writerFunc(func(p []byte) (int, error) {
		writes++
		if selector&256 != 0 {
			if writes > 1 {
				t.Error("write after refusal")
			}
			return 0, refused
		}
		return out.Write(p)
	})
	switch kind {
	case 0:
		img, e := n.Bitmap(ctx, page, o)
		err = e
		if e == nil && (img == nil || img.Rect.Empty() || len(img.Pix) != img.Stride*img.Rect.Dy()) {
			t.Fatal("invalid bitmap")
		}
	case 1:
		img, e := n.Raster(ctx, page, o)
		err = e
		if e == nil && (img == nil || img.Rect.Empty() || len(img.Gray) != img.Rect.Dx()*img.Rect.Dy() || len(img.Alpha) != len(img.Gray)) {
			t.Fatal("invalid raster")
		}
	case 2:
		data, err = n.PNG(ctx, page, o)
	case 3:
		data, err = n.SVG(ctx, page, o)
	case 4:
		data, err = n.PagePDF(ctx, page, o)
	case 5:
		data, err = n.PDF(ctx, page, min(4, count-page), o)
	case 6:
		err = n.WritePNG(ctx, writer, page, o)
	case 7:
		err = n.WriteSVG(ctx, writer, page, o)
	case 8:
		err = n.WritePagePDF(ctx, writer, page, o)
	case 9:
		err = n.WritePDF(ctx, writer, page, min(4, count-page), o)
	}
	if err != nil {
		if err == refused { //nolint:errorlint // Writer identity is part of the API contract.
			if writes != 1 {
				t.Fatal("refusal count", writes)
			}
			return
		}
		if errors.Is(err, refused) {
			t.Fatal("writer error identity lost", err)
		}
		documentedError(t, err)
		return
	}
	if kind < 2 {
		return
	}
	if kind >= 6 {
		if selector&256 != 0 {
			t.Fatal("refusing writer succeeded")
		}
		data = out.Bytes()
	}
	switch kind {
	case 2, 6:
		if !bytes.HasPrefix(data, []byte("\x89PNG\r\n\x1a\n")) {
			t.Fatal("missing PNG signature")
		}
		config, e := png.DecodeConfig(bytes.NewReader(data))
		if e != nil {
			t.Fatal(e)
		}
		g, e := n.PageGeometry(page)
		if e != nil {
			t.Fatal(e)
		}
		if config.Width != int(math.Ceil(g.Width*o.Scale)) || config.Height != int(math.Ceil(g.Height*o.Scale)) {
			t.Fatal("PNG dimensions")
		}
	case 3, 7:
		var root struct{ XMLName xml.Name }
		if err := xml.Unmarshal(data, &root); err != nil || root.XMLName.Local != "svg" {
			t.Fatal("missing SVG root")
		}
	default:
		if !bytes.HasPrefix(data, []byte("%PDF-")) {
			t.Fatal("missing PDF header")
		}
	}
}
