// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"context"
	"io"
	"testing"

	"github.com/altessa-s/snrender-go"
)

func benchmarkRender(b *testing.B, document bool) {
	n := open(b, "testdata/supernote-typescript/stroke-n5-20260016-isolation-tools-colors-widths.note")
	for _, cancellable := range []bool{false, true} {
		name := "background"
		if cancellable {
			name = "cancellable"
		}
		b.Run(name, func(b *testing.B) {
			ctx := context.Background()
			if cancellable {
				var cancel context.CancelFunc
				ctx, cancel = context.WithCancel(ctx)
				defer cancel()
			}
			b.ReportAllocs()
			b.ResetTimer()
			for range b.N {
				var err error
				if document {
					_, err = n.PDF(ctx, 0, -1, nil)
				} else {
					_, err = n.PNG(ctx, 0, nil)
				}
				if err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}

func BenchmarkPagePNG(b *testing.B)     { benchmarkRender(b, false) }
func BenchmarkDocumentPDF(b *testing.B) { benchmarkRender(b, true) }

// Synthetic input keeps throughput measurements independent of private notebooks.
func benchmarkWrite(b *testing.B, format string) {
	n := streamingNotebook(b)
	ctx := context.Background()
	o := &snrender.Options{Scale: .5}
	write := func(w io.Writer) error { return n.WriteSVG(ctx, w, 0, o) }
	if format == "PDF" {
		write = func(w io.Writer) error { return n.WritePDF(ctx, w, 0, 1, o) }
	}
	var w countingWriter
	b.ReportAllocs()
	b.ResetTimer()
	for range b.N {
		if err := write(&w); err != nil {
			b.Fatal(err)
		}
	}
	b.ReportMetric(float64(w.calls)/float64(b.N), "writes/op")
	b.SetBytes(int64(w.bytes / b.N))
}
func BenchmarkWriteSVG(b *testing.B) { benchmarkWrite(b, "SVG") }
func BenchmarkWritePDF(b *testing.B) { benchmarkWrite(b, "PDF") }
