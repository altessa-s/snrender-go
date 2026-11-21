// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"context"
	"errors"
	"fmt"
	"math"
	"sync"
	"testing"
	"unicode/utf8"

	"github.com/altessa-s/snrender-go"
)

func rotatedRect(r snrender.Rect, rotation int) snrender.Rect {
	switch rotation {
	case 90:
		return snrender.Rect{X: 200 - r.Y - r.Height, Y: r.X, Width: r.Height, Height: r.Width}
	case 180:
		return snrender.Rect{X: 300 - r.X - r.Width, Y: 200 - r.Y - r.Height, Width: r.Width, Height: r.Height}
	case 270:
		return snrender.Rect{X: r.Y, Y: 300 - r.X - r.Width, Width: r.Height, Height: r.Width}
	default:
		return r
	}
}

func TestPageTextGeometry(t *testing.T) {
	t.Parallel()
	for _, rotation := range []int{0, 90, 180, 270} {
		for _, offset := range []int{-1, 0, 1} {
			t.Run(fmt.Sprintf("r%d-offset%d", rotation, offset), func(t *testing.T) {
				pdf := read(t, fmt.Sprintf("testdata/pdf-text/r%d-offset%d.pdf", rotation, offset))
				for _, tc := range []struct {
					rects []snrender.Rect
					want  string
				}{
					{[]snrender.Rect{{X: 19, Y: 16, Width: 100, Height: 18}}, "Alpha Beta"},
					{[]snrender.Rect{{X: 20, Y: 16, Width: 7, Height: 18}}, "A"},
					{[]snrender.Rect{{X: 20, Y: 16, Width: 1, Height: 18}}, ""},
					{[]snrender.Rect{{X: 19, Y: 66, Width: 100, Height: 18}, {X: 19, Y: 41, Width: 100, Height: 18}, {X: 19, Y: 41, Width: 100, Height: 18}}, "multi-line words"},
					{[]snrender.Rect{{X: 19, Y: 91, Width: 100, Height: 18}}, "Жёлтый"},
					{[]snrender.Rect{{X: 19, Y: 116, Width: 100, Height: 18}}, "fi"},
					{nil, ""},
					{[]snrender.Rect{{IsNull: true}}, ""},
					{[]snrender.Rect{{X: -100, Y: -100, Width: 5, Height: 5}}, ""},
				} {
					rects := make([]snrender.Rect, len(tc.rects))
					for i, r := range tc.rects {
						rects[i] = rotatedRect(r, rotation)
						rects[i].IsNull = r.IsNull
					}
					got, err := snrender.PageText(context.Background(), pdf, 0, rects, nil)
					if err != nil || got != tc.want {
						t.Fatalf("%+v: %q, %v; want %q", rects, got, err, tc.want)
					}
				}
			})
		}
	}
}

func TestPageTextIndependentCases(t *testing.T) {
	for _, tc := range []struct {
		name, want string
		page       int
		rects      []snrender.Rect
	}{
		{"counter90", "Hello World", 0, []snrender.Rect{{Width: 400, Height: 300}}},
		{"counter270", "Hello World", 0, []snrender.Rect{{Width: 400, Height: 300}}},
		{"vertical", "Hello World", 0, []snrender.Rect{{Width: 300, Height: 400}}},
		{"columns-left", "left one\nleft two", 0, []snrender.Rect{{X: 15, Y: 55, Width: 90, Height: 20}, {X: 15, Y: 35, Width: 90, Height: 20}, {X: 15, Y: 35, Width: 90, Height: 20}}},
		{"columns-union", "left one right one\nleft two right two", 0, []snrender.Rect{{X: 210, Y: 30, Width: 100, Height: 50}, {X: 10, Y: 30, Width: 100, Height: 50}, {X: 10, Y: 30, Width: 100, Height: 50}}},
		{"astral", "x𝐀x😀x", 0, []snrender.Rect{{Width: 400, Height: 300}}},
		{"page-one", "Second page", 1, []snrender.Rect{{Width: 400, Height: 300}}},
		{"boundary-0-A", "A", 0, []snrender.Rect{{X: 26, Width: 10, Height: 10}}},
		{"boundary-0-empty", "", 0, []snrender.Rect{{X: 16, Width: 10, Height: 10}}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			got, err := snrender.PageText(context.Background(), read(t, "testdata/pdf-text/"+tc.name+".pdf"), tc.page, tc.rects, nil)
			if err != nil || got != tc.want {
				t.Fatalf("%q, %v; want %q", got, err, tc.want)
			}
		})
	}
}

func TestPageTextLimits(t *testing.T) {
	pdf := read(t, "testdata/pdf-text/r0-offset0.pdf")
	rects := []snrender.Rect{{X: 19, Y: 16, Width: 100, Height: 18}}
	for _, change := range []func(*snrender.TextOptions){
		func(o *snrender.TextOptions) { o.MaximumSourceBytes = len(pdf) - 1 },
		func(o *snrender.TextOptions) { o.MaximumCharacters = 1 },
		func(o *snrender.TextOptions) { o.Limits.MaximumWork = 1 },
		func(o *snrender.TextOptions) { o.Limits.MaximumDocumentWork = 1 },
		func(o *snrender.TextOptions) { o.Limits.MaximumOutputBytes = 10 },
	} {
		o := snrender.DefaultTextOptions()
		change(&o)
		if _, err := snrender.PageText(context.Background(), pdf, 0, rects, &o); !errors.Is(err, snrender.ErrLimit) {
			t.Fatal(err)
		}
	}
	for _, o := range []*snrender.TextOptions{nil, {}, {Limits: snrender.Limits{MaximumOutputBytes: 11}}} {
		got, err := snrender.PageText(context.Background(), pdf, 0, rects, o)
		if err != nil || got != "Alpha Beta" {
			t.Fatalf("%q %v", got, err)
		}
	}
	if _, err := snrender.PageText(context.Background(), pdf, 0, append(rects, rects...), &snrender.TextOptions{MaximumRectangles: 1}); !errors.Is(err, snrender.ErrLimit) {
		t.Fatal(err)
	}
	for _, o := range []*snrender.TextOptions{{MaximumSourceBytes: -1}, {MaximumRectangles: -1}, {MaximumCharacters: -1}, {Limits: snrender.Limits{MaximumWork: -1}}} {
		if _, err := snrender.PageText(context.Background(), pdf, 0, rects, o); !errors.Is(err, snrender.ErrInvalidArgument) {
			t.Fatal(err)
		}
	}
	for _, r := range []snrender.Rect{{Width: -1}, {Width: math.NaN()}, {X: math.Inf(1)}} {
		if _, err := snrender.PageText(context.Background(), pdf, 0, []snrender.Rect{r}, nil); !errors.Is(err, snrender.ErrInvalidArgument) {
			t.Fatal(err)
		}
	}
	for _, page := range []int{-1, 1} {
		if _, err := snrender.PageText(context.Background(), pdf, page, rects, nil); !errors.Is(err, snrender.ErrInvalidArgument) {
			t.Fatal(err)
		}
	}
	for _, data := range [][]byte{nil, []byte("bad")} {
		if _, err := snrender.PageText(context.Background(), data, 0, rects, nil); !errors.Is(err, snrender.ErrInvalidArgument) {
			t.Fatal(err)
		}
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := snrender.PageText(ctx, pdf, 0, rects, nil); !errors.Is(err, context.Canceled) || !errors.Is(err, snrender.ErrCancelled) {
		t.Fatal(err)
	}
	if _, err := snrender.PageText(nil, pdf, 0, rects, nil); !errors.Is(err, snrender.ErrInvalidArgument) { //nolint:staticcheck // Exercise invalid context validation.
		t.Fatal(err)
	}
}

func TestTextAndDrawingConcurrent(t *testing.T) {
	pdf := read(t, "testdata/pdf-text/r0-offset0.pdf")
	spd := read(t, "testdata/spd/valid.spd")
	var wg sync.WaitGroup
	for range 8 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for range 3 {
				text, err := snrender.PageText(context.Background(), pdf, 0, []snrender.Rect{{X: 19, Y: 16, Width: 100, Height: 18}}, nil)
				if err != nil || text != "Alpha Beta" {
					t.Errorf("text %q %v", text, err)
				}
				if _, err := snrender.DrawingPNG(context.Background(), spd, &snrender.DrawingOptions{Scale: .125}); err != nil {
					t.Error(err)
				}
			}
		}()
	}
	wg.Wait()
}

func FuzzPageText(f *testing.F) {
	f.Add(read(f, "testdata/pdf-text/r0-offset0.pdf"), uint8(0))
	f.Add(read(f, "testdata/pdf-text/astral.pdf"), uint8(1))
	f.Add([]byte("bad"), uint8(255))
	f.Fuzz(func(t *testing.T, data []byte, selector uint8) {
		if len(data) > 256<<10 {
			return
		}
		o := snrender.TextOptions{MaximumSourceBytes: 256 << 10, MaximumRectangles: 8, MaximumCharacters: 4096, Limits: snrender.Limits{MaximumPages: 4, MaximumWork: 1000000, MaximumDocumentWork: 1000000, MaximumOutputBytes: 16384}}
		r := snrender.Rect{Width: 400, Height: 300}
		if selector&1 != 0 {
			r.X = math.NaN()
		}
		text, err := snrender.PageText(context.Background(), data, int(selector>>6), []snrender.Rect{r}, &o)
		if err != nil {
			documentedError(t, err)
			return
		}
		if !utf8.ValidString(text) || len(text) >= o.Limits.MaximumOutputBytes {
			t.Fatal("invalid text output")
		}
	})
}
