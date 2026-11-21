// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"bytes"
	"context"
	"encoding/xml"
	"errors"
	"image"
	"image/png"
	"io"
	"math"
	"os"
	"runtime"
	"strings"
	"testing"

	"github.com/altessa-s/snrender-go"
)

func TestDrawingGeometryAndPixels(t *testing.T) {
	data := read(t, "testdata/spd/valid.spd")
	for _, o := range []*snrender.DrawingOptions{nil, {}, func() *snrender.DrawingOptions { v := snrender.DefaultDrawingOptions(); return &v }()} {
		b, err := snrender.DrawingBitmap(context.Background(), data, o)
		if err != nil {
			t.Fatal(err)
		}
		if b.Rect != image.Rect(0, 0, 512, 256) {
			t.Fatal(b.Rect)
		}
		if b.RGBAAt(138, 10).R != 100 || b.RGBAAt(266, 10).R != 32 || b.RGBAAt(394, 10).R != 0 {
			t.Fatal("tile axes/composition differ")
		}
	}
	o := snrender.DrawingOptions{Page: snrender.DrawingCanvas}
	b, err := snrender.DrawingBitmap(context.Background(), data, &o)
	if err != nil {
		t.Fatal(err)
	}
	if b.Rect != image.Rect(0, 0, 256, 256) {
		t.Fatal(b.Rect)
	}
	for _, p := range []struct{ x, y, want int }{{10, 10, 100}, {80, 10, 184}, {10, 80, 227}, {160, 10, 32}, {160, 160, 0}, {10, 160, 255}} {
		got := b.RGBAAt(p.x, p.y)
		if abs(int(got.R)-p.want) > 1 || got.R != got.G || got.R != got.B || got.A != 255 {
			t.Fatalf("pixel %d,%d: %v", p.x, p.y, got)
		}
	}
	o.HideReferenceLayer = true
	o.Background = &snrender.Color{Red: 1, Green: 1, Blue: 1, Alpha: 0}
	b, err = snrender.DrawingBitmap(context.Background(), data, &o)
	if err != nil {
		t.Fatal(err)
	}
	if got := b.RGBAAt(10, 80); got.R != 100 || got.A != 128 {
		t.Fatal(got)
	}
	if got := b.RGBAAt(160, 10); got.A != 0 {
		t.Fatal(got)
	}
	o = snrender.DrawingOptions{DPI: 64, Scale: 2}
	b, err = snrender.DrawingBitmap(context.Background(), data, &o)
	if err != nil || b.Rect != image.Rect(0, 0, 256, 128) {
		t.Fatalf("DPI overrides scale: %v %v", b, err)
	}
	b, err = snrender.DrawingBitmap(context.Background(), read(t, "testdata/spd/empty.spd"), nil)
	if err != nil || b.Rect != image.Rect(0, 0, 1, 1) {
		t.Fatalf("empty: %v %v", b, err)
	}
}

func abs(n int) int {
	if n < 0 {
		return -n
	}
	return n
}

func TestDrawingEncodersAndWriters(t *testing.T) {
	data := read(t, "testdata/spd/valid.spd")
	ctx := context.Background()
	for _, tc := range []struct {
		name   string
		encode func(context.Context, []byte, *snrender.DrawingOptions) ([]byte, error)
		write  func(context.Context, io.Writer, []byte, *snrender.DrawingOptions) error
	}{
		{"png", snrender.DrawingPNG, snrender.WriteDrawingPNG}, {"pdf", snrender.DrawingPDF, snrender.WriteDrawingPDF}, {"svg", snrender.DrawingSVG, snrender.WriteDrawingSVG},
	} {
		t.Run(tc.name, func(t *testing.T) {
			out, err := tc.encode(ctx, data, nil)
			if err != nil {
				t.Fatal(err)
			}
			var buf bytes.Buffer
			if err := tc.write(ctx, &buf, data, nil); err != nil {
				t.Fatal(err)
			}
			switch tc.name {
			case "png":
				a, err := png.Decode(bytes.NewReader(out))
				if err != nil {
					t.Fatal(err)
				}
				b, err := png.Decode(&buf)
				if err != nil {
					t.Fatal(err)
				}
				if a.Bounds() != image.Rect(0, 0, 512, 256) || a.At(138, 10) != b.At(138, 10) {
					t.Fatal("PNG output differs")
				}
			case "pdf":
				for _, b := range [][]byte{out, buf.Bytes()} {
					if !bytes.HasPrefix(b, []byte("%PDF-")) {
						t.Fatal("missing PDF header")
					}
				}
			case "svg":
				for _, b := range [][]byte{out, buf.Bytes()} {
					var root struct{ XMLName xml.Name }
					if err := xml.Unmarshal(b, &root); err != nil || root.XMLName.Local != "svg" {
						t.Fatal("bad SVG", err)
					}
				}
			}
			refused := errors.New("refused")
			calls := 0
			err = tc.write(ctx, writerFunc(func([]byte) (int, error) { calls++; return 0, refused }), data, nil)
			if err != refused || calls != 1 { //nolint:errorlint // Writer failures preserve exact identity.
				t.Fatalf("refusal %v calls %d", err, calls)
			}
			err = tc.write(ctx, writerFunc(func(p []byte) (int, error) { return len(p) - 1, nil }), data, nil)
			if !errors.Is(err, io.ErrShortWrite) {
				t.Fatal(err)
			}
			if err := tc.write(ctx, nil, data, nil); !errors.Is(err, snrender.ErrInvalidArgument) {
				t.Fatal(err)
			}
		})
	}
}

func TestDrawingDamagedTiles(t *testing.T) {
	for _, name := range []string{"bad", "crc", "wrong", "huge", "truncated", "decode", "adam7", "split"} {
		t.Run(name, func(t *testing.T) {
			data := read(t, "testdata/spd/"+name+".spd")
			if _, err := snrender.DrawingPNG(context.Background(), data, nil); err != nil {
				t.Fatal("skip damaged", err)
			}
			_, err := snrender.DrawingPNG(context.Background(), data, &snrender.DrawingOptions{StrictTiles: true})
			want := snrender.ErrBackend
			if name == "huge" {
				want = snrender.ErrLimit
			}
			if name == "adam7" || name == "split" {
				if err != nil {
					t.Fatal(err)
				}
			} else if !errors.Is(err, want) {
				t.Fatalf("%v want %v", err, want)
			}
		})
	}
}

func TestDrawingLimitsAndErrors(t *testing.T) {
	data := read(t, "testdata/spd/valid.spd")
	for _, change := range []func(*snrender.DrawingOptions){
		func(o *snrender.DrawingOptions) { o.MaximumTiles = 1 }, func(o *snrender.DrawingOptions) { o.MaximumDecodedBytes = 1 },
		func(o *snrender.DrawingOptions) { o.Limits.MaximumPixels = 1 }, func(o *snrender.DrawingOptions) { o.Limits.MaximumDecodedPixels = 1 },
		func(o *snrender.DrawingOptions) { o.Limits.MaximumWork = 1 }, func(o *snrender.DrawingOptions) { o.Limits.MaximumDocumentWork = 1 },
		func(o *snrender.DrawingOptions) { o.Limits.MaximumOutputBytes = 1 },
	} {
		o := snrender.DefaultDrawingOptions()
		change(&o)
		if _, err := snrender.DrawingPNG(context.Background(), data, &o); !errors.Is(err, snrender.ErrLimit) {
			t.Fatal(err)
		}
	}
	for _, o := range []*snrender.DrawingOptions{{Scale: -1}, {Scale: math.NaN()}, {DPI: -1}, {DPI: math.Inf(1)}, {Page: 99}, {Background: &snrender.Color{Alpha: 2}}, {MaximumTiles: -1}, {MaximumDecodedBytes: -1}, {Parse: snrender.DrawingParseOptions{Limits: snrender.DrawingParseLimits{MaximumFileSize: -1}}}} {
		if _, err := snrender.DrawingPNG(context.Background(), data, o); !errors.Is(err, snrender.ErrInvalidArgument) {
			t.Fatal(err)
		}
	}
	for _, change := range []func(*snrender.DrawingParseLimits){
		func(l *snrender.DrawingParseLimits) { l.MaximumFileSize = 1 }, func(l *snrender.DrawingParseLimits) { l.MaximumLayers = 1 },
		func(l *snrender.DrawingParseLimits) { l.MaximumTilesPerLayer = 1 }, func(l *snrender.DrawingParseLimits) { l.MaximumTileBytes = 1 },
		func(l *snrender.DrawingParseLimits) { l.MaximumTotalTileBytes = 1 }, func(l *snrender.DrawingParseLimits) { l.MaximumConfigEntries = 1 },
		func(l *snrender.DrawingParseLimits) { l.MaximumConfigBytes = 1 }, func(l *snrender.DrawingParseLimits) { l.MaximumMemoryBytes = 1 }, func(l *snrender.DrawingParseLimits) { l.MaximumWork = 1 },
	} {
		o := snrender.DefaultDrawingOptions()
		change(&o.Parse.Limits)
		_, err := snrender.DrawingPNG(context.Background(), data, &o)
		var pe *snrender.DrawingParseError
		if !errors.As(err, &pe) || !errors.Is(err, snrender.ErrParseLimit) || !errors.Is(err, snrender.ErrLimit) || pe.Status != snrender.ParseLimit {
			t.Fatal(err)
		}
	}
	_, err := snrender.DrawingPNG(context.Background(), bytes.Repeat([]byte("bad"), 64), nil)
	var pe *snrender.DrawingParseError
	if !errors.As(err, &pe) || !errors.Is(err, snrender.ErrUnsupportedFormat) || pe.Message == "" || !strings.Contains(err.Error(), "drawing parse") {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := snrender.DrawingPNG(ctx, data, nil); !errors.Is(err, context.Canceled) || !errors.Is(err, snrender.ErrCancelled) {
		t.Fatal(err)
	}
	if _, err := snrender.DrawingPNG(nil, data, nil); !errors.Is(err, snrender.ErrInvalidArgument) { //nolint:staticcheck // Exercise invalid context validation.
		t.Fatal(err)
	}
}

func TestDrawingAndTextAbnormalCallbacks(t *testing.T) {
	data := read(t, "testdata/spd/valid.spd")
	pdf := read(t, "testdata/pdf-text/r0-offset0.pdf")
	before := snrender.LiveHandles()
	for _, panics := range []bool{true, false} {
		for _, kind := range []string{"png", "pdf", "svg", "drawing cancel", "text cancel"} {
			done := make(chan struct{})
			go func() {
				defer close(done)
				defer func() {
					p := recover()
					if panics && p != "stop" {
						t.Errorf("panic %v", p)
					}
					if !panics && p != nil {
						t.Errorf("unexpected panic %v", p)
					}
				}()
				fn := func() {
					if panics {
						panic("stop")
					}
					runtime.Goexit()
				}
				w := writerFunc(func([]byte) (int, error) { fn(); return 0, nil })
				switch kind {
				case "png":
					_ = snrender.WriteDrawingPNG(context.Background(), w, data, nil)
				case "pdf":
					_ = snrender.WriteDrawingPDF(context.Background(), w, data, nil)
				case "svg":
					_ = snrender.WriteDrawingSVG(context.Background(), w, data, nil)
				case "drawing cancel":
					_, _ = snrender.DrawingPNG(callbackContext{context.Background(), fn}, data, nil)
				case "text cancel":
					_, _ = snrender.PageText(callbackContext{context.Background(), fn}, pdf, 0, []snrender.Rect{{Width: 300, Height: 200}}, nil)
				}
				t.Error("abnormal callback returned")
			}()
			<-done
			if snrender.LiveHandles() != before {
				t.Fatal("callback handle leak")
			}
		}
	}
}

func FuzzDrawing(f *testing.F) {
	for _, name := range []string{"valid", "bad", "huge", "empty", "adam7"} {
		data, err := os.ReadFile("testdata/spd/" + name + ".spd")
		if err != nil {
			f.Fatal(err)
		}
		f.Add(data, uint8(0))
	}
	f.Add([]byte("bad"), uint8(1))
	f.Fuzz(func(t *testing.T, data []byte, selector uint8) {
		if len(data) > 256<<10 {
			return
		}
		o := snrender.DrawingOptions{Scale: .125, StrictTiles: selector&1 != 0, HideReferenceLayer: selector&2 != 0, Limits: snrender.Limits{MaximumPixels: 65536, MaximumDecodedPixels: 1048576, MaximumWork: 8000000, MaximumDocumentWork: 8000000, MaximumOutputBytes: 1 << 20}, MaximumTiles: 64, MaximumDecodedBytes: 8 << 20, Parse: snrender.DrawingParseOptions{Limits: snrender.DrawingParseLimits{MaximumFileSize: 256 << 10, MaximumMemoryBytes: 8 << 20, MaximumWork: 8000000}}}
		out, err := snrender.DrawingPNG(context.Background(), data, &o)
		if err != nil {
			var pe *snrender.DrawingParseError
			if !errors.As(err, &pe) {
				documentedError(t, err)
			}
			return
		}
		c, err := png.DecodeConfig(bytes.NewReader(out))
		if err != nil || c.Width < 1 || c.Height < 1 || c.Width*c.Height > 65536 {
			t.Fatal("invalid PNG", err)
		}
	})
}
