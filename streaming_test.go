// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"bytes"
	"context"
	"errors"
	"image"
	"image/png"
	"io"
	"math/rand/v2"
	"testing"

	"github.com/altessa-s/snrender-go"
)

const streamBatch = 64 << 10

// Noise prevents compression from hiding excessive writer callbacks.
func streamingNotebook(t testing.TB) *snrender.Notebook {
	t.Helper()
	img := image.NewNRGBA(image.Rect(0, 0, 256, 256))
	rng := rand.New(rand.NewPCG(1, 2))
	for i := 0; i < len(img.Pix); i += 4 {
		img.Pix[i], img.Pix[i+1], img.Pix[i+2], img.Pix[i+3] = byte(rng.Uint32()), byte(rng.Uint32()), byte(rng.Uint32()), 255
	}
	var encoded bytes.Buffer
	if err := png.Encode(&encoded, img); err != nil {
		t.Fatal(err)
	}
	n, err := snrender.Open(imageNotebook("PNG", encoded.Bytes()))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = n.Close() })
	return n
}

type countingWriter struct{ calls, bytes int }

func (w *countingWriter) Write(p []byte) (int, error) {
	w.calls++
	w.bytes += len(p)
	return len(p), nil
}

func TestStreamingBatches(t *testing.T) {
	n := streamingNotebook(t)
	ctx := context.Background()
	for _, tc := range []struct {
		name   string
		encode func(context.Context, int, *snrender.Options) ([]byte, error)
		write  func(context.Context, io.Writer, int, *snrender.Options) error
	}{{"PNG", n.PNG, n.WritePNG}, {"SVG", n.SVG, n.WriteSVG}, {"PDF", n.PagePDF, n.WritePagePDF},
		{"DocumentPDF", func(ctx context.Context, page int, o *snrender.Options) ([]byte, error) {
			return n.PDF(ctx, page, 1, o)
		},
			func(ctx context.Context, w io.Writer, page int, o *snrender.Options) error {
				return n.WritePDF(ctx, w, page, 1, o)
			}}} {
		for _, scale := range []float64{.03125, .5} {
			t.Run(tc.name+"/"+map[bool]string{true: "small", false: "large"}[scale < .1], func(t *testing.T) {
				o := &snrender.Options{Scale: scale}
				expected, err := tc.encode(ctx, 0, o)
				if err != nil {
					t.Fatal(err)
				}
				var out bytes.Buffer
				var sizes []int
				err = tc.write(ctx, writerFunc(func(p []byte) (int, error) { sizes = append(sizes, len(p)); return out.Write(p) }), 0, o)
				if err != nil {
					t.Fatal(err)
				}
				if !sameOutput(expected, out.Bytes(), tc.name == "PDF" || tc.name == "DocumentPDF") {
					t.Fatal("stream differs from buffered output")
				}
				if scale > .1 && out.Len() <= streamBatch {
					t.Fatalf("large fixture too small: %d", out.Len())
				}
				want := (out.Len() + streamBatch - 1) / streamBatch
				if len(sizes) != want {
					t.Fatalf("%d writes for %d bytes, want %d", len(sizes), out.Len(), want)
				}
				for i, size := range sizes {
					if size > streamBatch || (i < len(sizes)-1 && size != streamBatch) {
						t.Fatalf("write %d: %d bytes", i, size)
					}
				}
				for _, failAt := range []int{1, want} {
					for _, short := range []bool{false, true} {
						refused := errors.New("writer refused")
						calls := 0
						err = tc.write(ctx, writerFunc(func(p []byte) (int, error) {
							calls++
							if calls == failAt {
								if short {
									return len(p) - 1, nil
								}
								return 0, refused
							}
							return len(p), nil
						}), 0, o)
						target := error(refused)
						if short {
							target = io.ErrShortWrite
						}
						if err != target || calls != failAt { //nolint:errorlint // Preserve exact writer error identity.
							t.Fatalf("failure at %d: %v, calls %d", failAt, err, calls)
						}
					}
				}
			})
		}
	}
}

func TestDrawingStreamingBatches(t *testing.T) {
	data := read(t, "testdata/spd/valid.spd")
	for _, tc := range []struct {
		name   string
		encode func(context.Context, []byte, *snrender.DrawingOptions) ([]byte, error)
		write  func(context.Context, io.Writer, []byte, *snrender.DrawingOptions) error
	}{{"PNG", snrender.DrawingPNG, snrender.WriteDrawingPNG}, {"SVG", snrender.DrawingSVG, snrender.WriteDrawingSVG}, {"PDF", snrender.DrawingPDF, snrender.WriteDrawingPDF}} {
		t.Run(tc.name, func(t *testing.T) {
			ctx := context.Background()
			expected, err := tc.encode(ctx, data, nil)
			if err != nil {
				t.Fatal(err)
			}
			var out bytes.Buffer
			calls := 0
			err = tc.write(ctx, writerFunc(func(p []byte) (int, error) { calls++; return out.Write(p) }), data, nil)
			if err != nil {
				t.Fatal(err)
			}
			if !sameOutput(expected, out.Bytes(), tc.name == "PDF" || tc.name == "DocumentPDF") {
				t.Fatal("stream differs")
			}
			if want := (out.Len() + streamBatch - 1) / streamBatch; calls != want {
				t.Fatalf("%d writes, want %d", calls, want)
			}
		})
	}
}

func TestStreamingDiscardsPendingBytesOnFailure(t *testing.T) {
	n := streamingNotebook(t)
	var w countingWriter
	err := n.WriteSVG(context.Background(), &w, 0, &snrender.Options{
		Scale: .03125, Limits: snrender.Limits{MaximumOutputBytes: 1},
	})
	if !errors.Is(err, snrender.ErrLimit) || w.calls != 0 {
		t.Fatalf("limit: %v, writes %d", err, w.calls)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	err = n.WritePDF(ctx, &w, 0, 1, &snrender.Options{Scale: .03125, Progress: func(done, _ int) {
		if done == 1 {
			cancel()
		}
	}})
	if !errors.Is(err, context.Canceled) || w.calls != 0 {
		t.Fatalf("cancellation: %v, writes %d", err, w.calls)
	}
}
