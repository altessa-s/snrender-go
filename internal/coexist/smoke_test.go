// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package coexist_test

import (
	"bytes"
	"context"
	"image/png"
	"os"
	"testing"
	"unicode/utf8"

	"github.com/altessa-s/snnote-go"
	"github.com/altessa-s/snrender-go"
)

// Only paths supplied explicitly through the environment are ever read. Private
// content is never copied into testdata or written to output files.
func TestRealDataSmoke(t *testing.T) {
	pdfPath, markPath, spdPath := os.Getenv("SNRENDER_SMOKE_PDF"), os.Getenv("SNRENDER_SMOKE_MARK"), os.Getenv("SNRENDER_SMOKE_SPD")
	if pdfPath == "" || markPath == "" || spdPath == "" {
		t.Skip("set SNRENDER_SMOKE_PDF, SNRENDER_SMOKE_MARK and SNRENDER_SMOKE_SPD")
	}
	pdf, err := os.ReadFile(pdfPath)
	if err != nil {
		t.Fatal("read PDF failed")
	}
	mark, err := os.ReadFile(markPath)
	if err != nil {
		t.Fatal("read mark failed")
	}
	n, err := snnote.Parse(mark)
	if err != nil {
		t.Fatal("parse mark failed")
	}
	if len(n.Highlights) == 0 {
		t.Fatal("no highlights")
	}
	for i, h := range n.Highlights {
		if h.PageNumber <= 0 {
			t.Fatal("highlight has no source page")
		}
		rects := make([]snrender.Rect, len(h.DocumentRects))
		for j, r := range h.DocumentRects {
			if r == nil {
				rects[j].IsNull = true
			} else {
				rects[j] = snrender.Rect{X: r.X, Y: r.Y, Width: r.Width, Height: r.Height}
			}
		}
		text, err := snrender.PageText(context.Background(), pdf, int(h.PageNumber-1), rects, nil)
		if err != nil {
			t.Fatal("extract highlight failed")
		}
		t.Logf("highlight %d: bytes=%d runes=%d", i, len(text), utf8.RuneCountInString(text))
	}
	data, err := os.ReadFile(spdPath)
	if err != nil {
		t.Fatal("read drawing failed")
	}
	out, err := snrender.DrawingPNG(context.Background(), data, nil)
	if err != nil {
		t.Fatal("render drawing failed")
	}
	config, err := png.DecodeConfig(bytes.NewReader(out))
	if err != nil {
		t.Fatal("decode PNG dimensions failed")
	}
	t.Logf("drawing PNG dimensions: %dx%d", config.Width, config.Height)
}
