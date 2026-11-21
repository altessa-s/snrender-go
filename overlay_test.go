// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"image"
	"image/png"
	"io"
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/altessa-s/snrender-go"
)

const overlayMark = "testdata/samples/pdf-highlights-n6-20260016.mark"

// Coordinates come from the fixture JSON, independently of C's mapping code.
type documentRect struct{ X0, Y0, X1, Y1 float64 }
type fixtureHighlight struct {
	AnnotationType int            `json:"annotationType"`
	Rects          []documentRect `json:"mupdfRectList"`
}

func markHighlights(t *testing.T, path string) (int, []documentRect) {
	t.Helper()
	raw := read(t, path)
	size := binary.LittleEndian.Uint32(raw[24:28])
	decoded, err := base64.StdEncoding.DecodeString(string(raw[28 : 28+size]))
	if err != nil {
		t.Fatal(err)
	}
	var pages map[string][]fixtureHighlight
	if err := json.Unmarshal(decoded, &pages); err != nil {
		t.Fatal(err)
	}
	if len(pages) != 1 {
		t.Fatalf("expected one page in fixture, got %d", len(pages))
	}
	for key, items := range pages {
		page, err := strconv.Atoi(key)
		if err != nil {
			t.Fatal(err)
		}
		var rects []documentRect
		for _, item := range items {
			if item.AnnotationType == 0 {
				rects = append(rects, item.Rects...)
			}
		}
		return page, rects
	}
	panic("empty fixture")
}

func TestOverlayFixtureDigests(t *testing.T) {
	for _, line := range strings.Split(strings.TrimSpace(string(read(t, "testdata/overlay/SHA256SUMS"))), "\n") {
		fields := strings.Fields(line)
		if got := fmt.Sprintf("%x", sha256.Sum256(read(t, "testdata/overlay/"+fields[1]))); got != fields[0] {
			t.Errorf("%s digest %s, want %s", fields[1], got, fields[0])
		}
	}
}

func TestOverlayDeviceGeometry(t *testing.T) {
	for _, tc := range []struct {
		name          string
		width, height int
	}{
		{"pdf-highlights-n6-20260016.mark", 612, 792},
		{"pdf-ink-highlights-tall-n6-20260016.mark", 522, 756},
		{"pdf-ink-highlights-n5-20260016.mark", 540, 666},
	} {
		t.Run(tc.name, func(t *testing.T) {
			n := open(t, "testdata/samples/"+tc.name)
			unrelated := open(t, "testdata/samples/epub-n6-20260016.mark")
			pdf := read(t, "testdata/overlay/"+tc.name+".pdf")
			page, rects := markHighlights(t, "testdata/samples/"+tc.name)
			if has, err := n.HasAnnotations(page); err != nil || !has {
				t.Fatalf("HasAnnotations(%d): %v, %v", page, has, err)
			}
			if has, err := n.HasAnnotations(page + 100); err != nil || has {
				t.Fatalf("sparse query: %v, %v", has, err)
			}
			for _, scale := range []int{1, 2} {
				var report snrender.OverlayResult
				o := &snrender.OverlayOptions{Strict: true, Render: snrender.Options{Scale: float64(scale)}, Result: &report}
				bitmap, err := n.OverlayBitmap(context.Background(), pdf, page, o)
				if err != nil {
					t.Fatal(err)
				}
				if bitmap.Rect != image.Rect(0, 0, tc.width*scale, tc.height*scale) {
					t.Fatalf("bitmap dimensions %v", bitmap.Rect)
				}
				if report.AnnotatedPagesDrawn != 1 || report.PagesCheckedByPairs != 1 || report.WorstPairedResidual > 3 || report.NoPairsAgree || report.AllAnnotationsDropped {
					t.Fatalf("report %+v", report)
				}
				for _, rect := range rects {
					x, y := int((rect.X0+rect.X1)/2*float64(scale)), int((rect.Y0+rect.Y1)/2*float64(scale))
					p := bitmap.RGBAAt(x, y)
					if p.B >= 230 || p.B <= p.R || p.A != 255 {
						t.Fatalf("marker at %d,%d: %v; want darkened blue", x, y, p)
					}
				}
				encoded, err := n.OverlayPNG(context.Background(), pdf, page, o)
				if err != nil {
					t.Fatal(err)
				}
				decoded, err := png.Decode(bytes.NewReader(encoded))
				if err != nil {
					t.Fatal(err)
				}
				if decoded.Bounds() != bitmap.Rect {
					t.Fatal("PNG bounds differ")
				}
				for y := range bitmap.Rect.Dy() {
					for x := range bitmap.Rect.Dx() {
						r, g, b, a := decoded.At(x, y).RGBA()
						p := bitmap.RGBAAt(x, y)
						if r>>8 != uint32(p.R) || g>>8 != uint32(p.G) || b>>8 != uint32(p.B) || a>>8 != uint32(p.A) {
							t.Fatalf("PNG pixels differ at %d,%d", x, y)
						}
					}
				}
				var pngOut bytes.Buffer
				if err := n.WriteOverlayPNG(context.Background(), &pngOut, pdf, page, o); err != nil || !bytes.Equal(encoded, pngOut.Bytes()) {
					t.Fatalf("PNG writer differs: %v", err)
				}
			}
			var report snrender.OverlayResult
			o := &snrender.OverlayOptions{Strict: true, Result: &report}
			out, err := n.OverlayPDF(context.Background(), pdf, 0, snrender.AllPages, o)
			if err != nil || !bytes.HasPrefix(out, pdf) || len(out) <= len(pdf) || report.AnnotatedPagesDrawn != 1 {
				t.Fatalf("incremental source preservation: %v, %+v", err, report)
			}
			// Read the actual exported PDF through the native source renderer. The
			// unrelated mark names page 22, so it adds no ink on these pages.
			if has, err := unrelated.HasAnnotations(page); err != nil || has {
				t.Fatalf("unrelated mark: %v, %v", has, err)
			}
			exported, err := unrelated.OverlayBitmap(context.Background(), out, page, nil)
			if err != nil {
				t.Fatal(err)
			}
			for _, rect := range rects {
				x, y := int((rect.X0+rect.X1)/2), int((rect.Y0+rect.Y1)/2)
				p := exported.RGBAAt(x, y)
				if p.B >= 230 || p.B <= p.R {
					t.Fatalf("exported PDF marker at %d,%d: %v", x, y, p)
				}
			}
			var writer bytes.Buffer
			if err := n.WriteOverlayPDF(context.Background(), &writer, pdf, 0, snrender.AllPages, o); err != nil || !bytes.Equal(out, writer.Bytes()) {
				t.Fatalf("PDF writer differs: %v", err)
			}
			// Ownership survives closing the mark, and the input was never changed.
			_ = n.Close()
			if !bytes.Equal(pdf, read(t, "testdata/overlay/"+tc.name+".pdf")) || !bytes.HasPrefix(out, pdf) {
				t.Fatal("source or result lifetime")
			}
		})
	}
}

func TestOverlaySourcePagesAndDiagnostics(t *testing.T) {
	n := open(t, overlayMark)
	ctx := context.Background()
	pdf := read(t, "testdata/overlay/mixed.pdf")
	// Stored mark sheets do not bound the source document's range.
	count, err := n.PageCount()
	if err != nil || count != 1 {
		t.Fatalf("stored sheets %d, %v", count, err)
	}
	for _, selection := range []struct{ first, count int }{{1, 1}, {2, snrender.AllPages}} {
		var report snrender.OverlayResult
		o := &snrender.OverlayOptions{Result: &report}
		got, err := n.OverlayPDF(ctx, pdf, selection.first, selection.count, o)
		if err != nil || !bytes.HasPrefix(got, []byte("%PDF-")) || report.AnnotatedPagesDrawn != 0 || bytes.HasPrefix(got, pdf) {
			t.Fatalf("range %+v: %v, %+v", selection, err, report)
		}
		var writer bytes.Buffer
		if err := n.WriteOverlayPDF(ctx, &writer, pdf, selection.first, selection.count, o); err != nil || !bytes.Equal(got, writer.Bytes()) {
			t.Fatalf("range writer: %v", err)
		}
	}
	rotated, err := n.OverlayBitmap(ctx, pdf, 1, nil)
	if err != nil || rotated.Rect != image.Rect(0, 0, 660, 470) {
		t.Fatalf("rotated CropBox: %v, %v", rotated, err)
	}
	// Handwriting on source page 22: all sheets drop against the tiny source;
	// strictness cannot prove identity for a mark without paired rectangles.
	epub := open(t, "testdata/samples/epub-n6-20260016.mark")
	small := read(t, "testdata/overlay/source.pdf")
	var report snrender.OverlayResult
	o := &snrender.OverlayOptions{Result: &report}
	out, err := epub.OverlayPDF(ctx, small, 0, snrender.AllPages, o)
	if err != nil || !bytes.Equal(out, small) || !report.AllAnnotationsDropped || report.SheetsDropped != 1 || report.NoPairsAgree || report.PagesCheckedByPairs != 0 {
		t.Fatalf("dropped report %+v, %v", report, err)
	}
	o.Strict = true
	if _, err := epub.OverlayPDF(ctx, small, 0, snrender.AllPages, o); !errors.Is(err, snrender.ErrInvalidArgument) {
		t.Fatalf("strict dropped sheet: %v", err)
	}
	if report.SheetsDropped != 0 { // Reset, rather than carrying the previous call's result.
		t.Fatalf("stale report %+v", report)
	}
	o.Strict = false
	if _, err := n.OverlayPDF(ctx, small, 0, snrender.AllPages, o); err != nil || !report.NoPairsAgree || report.WorstPairedResidual <= 3 {
		t.Fatalf("mismatched pairs %+v, %v", report, err)
	}
	o.Strict = true
	if _, err := n.OverlayPDF(ctx, small, 0, snrender.AllPages, o); !errors.Is(err, snrender.ErrInvalidArgument) || report.WorstPairedResidual <= 3 {
		t.Fatalf("partial strict diagnostics %+v, %v", report, err)
	}
}

func TestOverlayResultOnFailure(t *testing.T) {
	n := open(t, overlayMark)
	pdf := read(t, "testdata/overlay/signed.pdf")
	sentinel := snrender.OverlayResult{
		AnnotatedPagesDrawn: 91, SheetsDropped: 92, HighlightsDropped: 93,
		WorstPairedResidual: 94.5, PagesCheckedByPairs: 95, SignatureFields: 96,
		AllAnnotationsDropped: true, NoPairsAgree: true,
	}
	for _, tc := range []struct {
		name string
		call func(*snrender.OverlayOptions) error
	}{
		{"bitmap", func(o *snrender.OverlayOptions) error {
			_, err := n.OverlayBitmap(context.Background(), pdf, 0, o)
			return err
		}},
		{"png", func(o *snrender.OverlayOptions) error {
			_, err := n.OverlayPNG(context.Background(), pdf, 0, o)
			return err
		}},
		{"png-writer", func(o *snrender.OverlayOptions) error {
			return n.WriteOverlayPNG(context.Background(), io.Discard, pdf, 0, o)
		}},
		{"pdf", func(o *snrender.OverlayOptions) error {
			_, err := n.OverlayPDF(context.Background(), pdf, 0, snrender.AllPages, o)
			return err
		}},
		{"pdf-writer", func(o *snrender.OverlayOptions) error {
			return n.WriteOverlayPDF(context.Background(), io.Discard, pdf, 0, snrender.AllPages, o)
		}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			t.Run("C-limit", func(t *testing.T) {
				result := sentinel
				o := &snrender.OverlayOptions{
					Render: snrender.Options{Limits: snrender.Limits{MaximumPixels: 1}},
					Result: &result,
				}
				// C discovers the signature before the page pixel limit fails.
				// Every sentinel field must be replaced, retaining that partial work.
				if err := tc.call(o); !errors.Is(err, snrender.ErrLimit) {
					t.Fatalf("error %v, want ErrLimit", err)
				}
				if want := (snrender.OverlayResult{SignatureFields: 1}); result != want {
					t.Fatalf("partial result %+v, want %+v", result, want)
				}
			})
			t.Run("Go-validation", func(t *testing.T) {
				result := sentinel
				o := &snrender.OverlayOptions{
					Limits: snrender.OverlayLimits{MaximumSourceBytes: -1},
					Result: &result,
				}
				if err := tc.call(o); !errors.Is(err, snrender.ErrInvalidArgument) {
					t.Fatalf("error %v, want ErrInvalidArgument", err)
				}
				if result != sentinel {
					t.Fatalf("Go validation changed result to %+v, want %+v", result, sentinel)
				}
			})
		})
	}
}

func TestOverlayValidationAndLimits(t *testing.T) {
	n := open(t, overlayMark)
	note := open(t, fixture)
	pdf := read(t, "testdata/overlay/mixed.pdf")
	ctx := context.Background()
	cancelled, cancel := context.WithCancel(ctx)
	cancel()
	for _, tc := range []struct {
		name string
		o    snrender.OverlayOptions
		want error
	}{
		{"source-bytes", snrender.OverlayOptions{Limits: snrender.OverlayLimits{MaximumSourceBytes: len(pdf) - 1}}, snrender.ErrLimit},
		{"negative-source", snrender.OverlayOptions{Limits: snrender.OverlayLimits{MaximumSourceBytes: -1}}, snrender.ErrInvalidArgument},
		{"pixels", snrender.OverlayOptions{Render: snrender.Options{Limits: snrender.Limits{MaximumPixels: 1}}}, snrender.ErrLimit},
		{"pages", snrender.OverlayOptions{Render: snrender.Options{Limits: snrender.Limits{MaximumPages: 1}}}, snrender.ErrLimit},
		{"work", snrender.OverlayOptions{Render: snrender.Options{Limits: snrender.Limits{MaximumWork: 1}}}, snrender.ErrLimit},
		{"document-work", snrender.OverlayOptions{Render: snrender.Options{Limits: snrender.Limits{MaximumDocumentWork: 1}}}, snrender.ErrLimit},
		{"output", snrender.OverlayOptions{Render: snrender.Options{Limits: snrender.Limits{MaximumOutputBytes: 1}}}, snrender.ErrLimit},
		{"negative-render", snrender.OverlayOptions{Render: snrender.Options{Limits: snrender.Limits{MaximumPixels: -1}}}, snrender.ErrInvalidArgument},
		{"scale", snrender.OverlayOptions{Render: snrender.Options{Scale: -1}}, snrender.ErrInvalidArgument},
		{"title", snrender.OverlayOptions{Render: snrender.Options{Title: "bad\x00title"}}, snrender.ErrInvalidArgument},
		{"epub", snrender.OverlayOptions{SourceKind: snrender.DocumentEPUB}, snrender.ErrUnsupported},
		{"kind", snrender.OverlayOptions{SourceKind: snrender.DocumentKind(99)}, snrender.ErrInvalidArgument},
	} {
		t.Run(tc.name, func(t *testing.T) {
			_, err := n.OverlayPDF(ctx, pdf, 0, snrender.AllPages, &tc.o)
			if !errors.Is(err, tc.want) {
				t.Fatalf("PDF: %v, want %v", err, tc.want)
			}
			if _, err := n.OverlayPNG(ctx, pdf, 0, &tc.o); !errors.Is(err, tc.want) {
				t.Fatalf("PNG: %v, want %v", err, tc.want)
			}
		})
	}
	for _, data := range [][]byte{nil, {}, []byte("bad"), pdf[:100], bytes.ReplaceAll(pdf, []byte("/Pages"), []byte("/Bogus"))} {
		if _, err := n.OverlayPDF(ctx, data, 0, snrender.AllPages, nil); err == nil {
			t.Fatal("invalid PDF accepted")
		}
	}
	for _, name := range []string{"encrypted", "huge", "empty"} {
		if _, err := n.OverlayPDF(ctx, read(t, "testdata/overlay/"+name+".pdf"), 0, snrender.AllPages, nil); err == nil {
			t.Fatalf("%s PDF accepted", name)
		}
	}
	for _, tc := range []struct{ first, count int }{{-1, 1}, {4, 1}, {0, 0}, {0, -2}, {3, 2}} {
		if _, err := n.OverlayPDF(ctx, pdf, tc.first, tc.count, nil); !errors.Is(err, snrender.ErrInvalidArgument) {
			t.Fatalf("range %+v: %v", tc, err)
		}
	}
	checks := []func() error{
		func() error { _, err := n.OverlayPNG(nil, pdf, 0, nil); return err }, //nolint:staticcheck // Exercise invalid context validation.
		func() error { _, err := n.OverlayBitmap(ctx, pdf, -1, nil); return err },
		func() error { _, err := n.OverlayBitmap(ctx, pdf, 4, nil); return err },
		func() error { _, err := n.HasAnnotations(-1); return err },
		func() error { _, err := note.HasAnnotations(0); return err },
		func() error { _, err := note.OverlayPDF(ctx, pdf, 0, 1, nil); return err },
		func() error { return n.WriteOverlayPDF(ctx, nil, pdf, 0, 1, nil) },
		func() error { return n.WriteOverlayPNG(ctx, nil, pdf, 0, nil) },
	}
	for i, check := range checks {
		if err := check(); !errors.Is(err, snrender.ErrInvalidArgument) {
			t.Errorf("validation %d: %v", i, err)
		}
	}
	for _, check := range []func() error{
		func() error { _, err := n.OverlayBitmap(cancelled, pdf, 0, nil); return err },
		func() error { _, err := n.OverlayPNG(cancelled, pdf, 0, nil); return err },
		func() error { _, err := n.OverlayPDF(cancelled, pdf, 0, snrender.AllPages, nil); return err },
		func() error { return n.WriteOverlayPNG(cancelled, io.Discard, pdf, 0, nil) },
		func() error { return n.WriteOverlayPDF(cancelled, io.Discard, pdf, 0, snrender.AllPages, nil) },
	} {
		if err := check(); !errors.Is(err, snrender.ErrCancelled) || !errors.Is(err, context.Canceled) {
			t.Errorf("cancellation: %v", err)
		}
	}
	defaults := snrender.DefaultOverlayOptions()
	if defaults.Limits.MaximumSourceBytes != 512<<20 || defaults.SourceKind != snrender.DocumentPDF || defaults.Result != nil || defaults.Render.Scale != snrender.DefaultOptions().Scale {
		t.Fatalf("defaults %+v", defaults)
	}
	for _, o := range []*snrender.OverlayOptions{nil, {}, &defaults, {Limits: snrender.OverlayLimits{MaximumSourceBytes: len(pdf)}}} {
		if _, err := n.OverlayPDF(ctx, pdf, 0, snrender.AllPages, o); err != nil {
			t.Fatalf("defaults / exact source limit: %v", err)
		}
	}
	_ = n.Close()
	if _, err := n.OverlayPDF(ctx, pdf, 0, 1, nil); !errors.Is(err, snrender.ErrClosed) {
		t.Fatal(err)
	}
	if _, err := (*snrender.Notebook)(nil).HasAnnotations(0); !errors.Is(err, snrender.ErrClosed) {
		t.Fatal(err)
	}
}

func TestOverlayWritersAndCancellation(t *testing.T) {
	n := open(t, overlayMark)
	pdf := read(t, "testdata/overlay/mixed.pdf")
	refused := errors.New("writer refused")
	for _, write := range []func(context.Context, io.Writer, *snrender.OverlayOptions) error{
		func(ctx context.Context, w io.Writer, o *snrender.OverlayOptions) error {
			return n.WriteOverlayPNG(ctx, w, pdf, 0, o)
		},
		func(ctx context.Context, w io.Writer, o *snrender.OverlayOptions) error {
			return n.WriteOverlayPDF(ctx, w, pdf, 0, snrender.AllPages, o)
		},
	} {
		for _, short := range []bool{false, true} {
			calls := 0
			err := write(context.Background(), writerFunc(func(p []byte) (int, error) {
				calls++
				if short {
					return len(p) - 1, nil
				}
				return 0, refused
			}), nil)
			want := refused
			if short {
				want = io.ErrShortWrite
			}
			if err != want || calls != 1 { //nolint:errorlint // Preserve exact writer error identity.
				t.Fatalf("writer error %v, calls %d", err, calls)
			}
		}
		// Once the output limit wins, a refused closing write cannot replace it.
		if err := write(context.Background(), writerFunc(func([]byte) (int, error) { return 0, refused }), &snrender.OverlayOptions{Render: snrender.Options{Limits: snrender.Limits{MaximumOutputBytes: 1}}}); !errors.Is(err, snrender.ErrLimit) {
			t.Fatalf("first failure: %v", err)
		}
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	var progress []int
	_, err := n.OverlayPDF(ctx, pdf, 0, snrender.AllPages, &snrender.OverlayOptions{Render: snrender.Options{Progress: func(completed, total int) {
		if total != 4 {
			t.Errorf("total %d", total)
		}
		progress = append(progress, completed)
		if completed == 1 {
			cancel()
		}
	}}})
	if !errors.Is(err, context.Canceled) || !errors.Is(err, snrender.ErrCancelled) || len(progress) != 2 || progress[0] != 0 || progress[1] != 1 {
		t.Fatalf("progress %v, cancellation %v", progress, err)
	}
}

func TestOverlayConcurrencyAndCallbacks(t *testing.T) {
	n := open(t, overlayMark)
	pdf := read(t, "testdata/overlay/mixed.pdf")
	before := snrender.LiveHandles()
	var wg sync.WaitGroup
	for range 12 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			var result snrender.OverlayResult
			o := &snrender.OverlayOptions{Render: snrender.Options{Scale: .125}, Result: &result}
			if _, err := n.OverlayBitmap(context.Background(), pdf, 0, o); err != nil {
				t.Error(err)
			}
			if err := n.WriteOverlayPDF(context.Background(), io.Discard, pdf, 0, snrender.AllPages, o); err != nil || result.AnnotatedPagesDrawn != 1 {
				t.Errorf("concurrent PDF: %v, %+v", err, result)
			}
		}()
	}
	wg.Wait()
	for _, kind := range []string{"png-writer", "pdf-writer", "cancel", "progress"} {
		for _, panics := range []bool{false, true} {
			t.Run(fmt.Sprintf("%s/panic=%t", kind, panics), func(t *testing.T) {
				wg.Add(1)
				go func() {
					defer wg.Done()
					defer func() {
						p := recover()
						if panics && p != "overlay stop" || !panics && p != nil {
							t.Errorf("panic %v", p)
						}
					}()
					fn := func() {
						if panics {
							panic("overlay stop")
						}
						runtime.Goexit()
					}
					o := &snrender.OverlayOptions{Render: snrender.Options{Scale: .125}}
					w := writerFunc(func(p []byte) (int, error) { fn(); return len(p), nil })
					switch kind {
					case "png-writer":
						_ = n.WriteOverlayPNG(context.Background(), w, pdf, 0, o)
					case "pdf-writer":
						_ = n.WriteOverlayPDF(context.Background(), w, pdf, 0, 1, o)
					case "cancel":
						_, _ = n.OverlayBitmap(callbackContext{context.Background(), fn}, pdf, 0, o)
					case "progress":
						o.Render.Progress = func(int, int) { fn() }
						_, _ = n.OverlayPDF(context.Background(), pdf, 0, 1, o)
					}
					t.Error("abnormal callback returned")
				}()
				wg.Wait()
			})
		}
	}
	if snrender.LiveHandles() != before {
		t.Fatalf("callback handles leaked: %d", snrender.LiveHandles()-before)
	}
	// The PDFium lock is released before output and application callbacks, so
	// an independent preview can run there. Closing only defers C-model release.
	var once sync.Once
	if err := n.WriteOverlayPNG(context.Background(), writerFunc(func(p []byte) (int, error) {
		once.Do(func() {
			if _, err := n.OverlayPNG(context.Background(), pdf, 1, nil); err != nil {
				t.Error(err)
			}
			_ = n.Close()
		})
		return len(p), nil
	}), pdf, 0, nil); err != nil {
		t.Fatal(err)
	}
}

func TestOverlayPDFSeeds(t *testing.T) {
	n := open(t, overlayMark)
	for _, name := range []string{"acroform", "appearances", "form", "incremental", "inherited", "rich"} {
		t.Run(name, func(t *testing.T) {
			pdf := read(t, "testdata/overlay/"+name+".pdf")
			var result snrender.OverlayResult
			o := &snrender.OverlayOptions{Result: &result}
			if _, err := n.OverlayPNG(context.Background(), pdf, 0, o); err != nil {
				t.Fatal(err)
			}
			got, err := n.OverlayPDF(context.Background(), pdf, 0, snrender.AllPages, o)
			if err != nil || !bytes.HasPrefix(got, pdf) {
				t.Fatalf("preserved PDF seed: %v", err)
			}
		})
	}
}

func TestOverlaySignaturesAndAppearances(t *testing.T) {
	n := open(t, overlayMark)
	unrelated := open(t, "testdata/samples/epub-n6-20260016.mark")
	var warnings atomic.Int64
	snrender.SetLogger(func(level snrender.LogLevel, message string) {
		if level == snrender.LogWarning && strings.Contains(message, "signature fields") {
			warnings.Add(1)
		}
	})
	defer snrender.SetLogger(nil)
	pdf := read(t, "testdata/overlay/signed.pdf")
	var result snrender.OverlayResult
	got, err := n.OverlayPDF(context.Background(), pdf, 0, snrender.AllPages, &snrender.OverlayOptions{Result: &result})
	if err != nil || !bytes.HasPrefix(got, pdf) || result.SignatureFields != 1 || warnings.Load() != 1 || snrender.LogWarning.String() != "warning" {
		t.Fatalf("signature report %+v, warnings %d, error %v", result, warnings.Load(), err)
	}
	// These source-only checks use the same probes as check_overlay_geometry.py.
	bitmap, err := unrelated.OverlayBitmap(context.Background(), read(t, "testdata/overlay/appearance.pdf"), 0, &snrender.OverlayOptions{Render: snrender.Options{Scale: 2}})
	if err != nil {
		t.Fatal(err)
	}
	if p := bitmap.RGBAAt(80, 220); p.R <= 240 || p.G >= 30 || p.B >= 30 {
		t.Fatalf("appearance-only red stamp: %v", p)
	}
	for _, name := range []string{"appearances", "acroform"} {
		bitmap, err := unrelated.OverlayBitmap(context.Background(), read(t, "testdata/overlay/"+name+".pdf"), 0, nil)
		if err != nil {
			t.Fatal(err)
		}
		if p := bitmap.RGBAAt(120, 280); p.G <= 200 || p.R >= 60 || p.B >= 60 {
			t.Fatalf("%s green widget: %v", name, p)
		}
		if p := bitmap.RGBAAt(40, 355); p.R <= 200 || p.G >= 60 || p.B >= 60 {
			t.Fatalf("%s red stamp: %v", name, p)
		}
		for _, point := range []image.Point{{175, 225}, {225, 175}} {
			if p := bitmap.RGBAAt(point.X, point.Y); p.R <= 240 || p.G <= 240 || p.B <= 240 {
				t.Fatalf("%s hidden/popup at %v: %v", name, point, p)
			}
		}
	}
}

// waitForPDFiumLock observes the C lock callback parked on the shared mutex.
// A slow preview that never calls Lock must fail, rather than satisfy a delay.
func waitForPDFiumLock(t *testing.T, done <-chan error) {
	t.Helper()
	timeout := time.NewTimer(10 * time.Second)
	defer timeout.Stop()
	poll := time.NewTicker(time.Millisecond)
	defer poll.Stop()
	stacks := make([]byte, 64<<10)
	for {
		n := runtime.Stack(stacks, true)
		for n == len(stacks) {
			stacks = make([]byte, 2*len(stacks))
			n = runtime.Stack(stacks, true)
		}
		for _, stack := range bytes.Split(stacks[:n], []byte("\n\n")) {
			header, _, _ := bytes.Cut(stack, []byte("\n"))
			if bytes.Contains(header, []byte("[sync.Mutex.Lock")) &&
				bytes.Contains(stack, []byte("snrender-go.snrgoPDFiumLock(")) {
				return
			}
		}
		select {
		case err := <-done:
			t.Fatalf("preview completed without waiting on shared lock: %v", err)
		case <-timeout.C:
			t.Fatalf("preview never observed waiting in PDFium lock callback:\n%s", stacks[:n])
		case <-poll.C:
		}
	}
}

// Installation is process-lifetime state, so success and late/repeated attempts
// run in fresh processes, independent of test order and -shuffle.
func TestPDFiumLock(t *testing.T) {
	if mode := os.Getenv("SNRENDER_LOCK_TEST"); mode != "" {
		mu := new(sync.Mutex)
		if err := snrender.SetPDFiumLock(nil); !errors.Is(err, snrender.ErrInvalidArgument) {
			t.Fatal(err)
		}
		n := open(t, overlayMark)
		pdf := read(t, "testdata/overlay/mixed.pdf")
		if mode == "late" {
			if _, err := n.OverlayPNG(context.Background(), pdf, 0, nil); err != nil {
				t.Fatal(err)
			}
		}
		err := snrender.SetPDFiumLock(mu)
		if runtime.GOOS == "darwin" && testTags == "" {
			if !errors.Is(err, snrender.ErrUnsupported) {
				t.Fatal(err)
			}
			return
		}
		if mode == "late" {
			if !errors.Is(err, snrender.ErrInvalidArgument) {
				t.Fatal(err)
			}
			return
		}
		if err != nil {
			t.Fatal(err)
		}
		if err := snrender.SetPDFiumLock(new(sync.Mutex)); !errors.Is(err, snrender.ErrInvalidArgument) {
			t.Fatalf("repeated installation: %v", err)
		}
		done := make(chan error, 1)
		func() {
			mu.Lock()
			defer mu.Unlock()
			go func() { _, err := n.OverlayPNG(context.Background(), pdf, 0, nil); done <- err }()
			waitForPDFiumLock(t, done)
			// PDF export does not acquire the host lock.
			if _, err := n.OverlayPDF(context.Background(), pdf, 0, 1, nil); err != nil {
				t.Error(err)
			}
			select {
			case err := <-done:
				t.Fatalf("preview completed while shared lock held: %v", err)
			default:
			}
		}()
		select {
		case err := <-done:
			if err != nil {
				t.Fatal(err)
			}
		case <-time.After(10 * time.Second):
			t.Fatal("preview blocked after shared unlock")
		}
		var once sync.Once
		if err := n.WriteOverlayPNG(context.Background(), writerFunc(func(p []byte) (int, error) {
			once.Do(func() {
				if _, err := n.OverlayPNG(context.Background(), pdf, 1, nil); err != nil {
					t.Error(err)
				}
			})
			return len(p), nil
		}), pdf, 0, nil); err != nil {
			t.Fatal(err)
		}
		return
	}
	for _, mode := range []string{"install", "late"} {
		cmd := exec.Command(os.Args[0], "-test.run=^TestPDFiumLock$", "-test.timeout=30s")
		cmd.Env = append(os.Environ(), "SNRENDER_LOCK_TEST="+mode)
		if out, err := cmd.CombinedOutput(); err != nil {
			t.Fatalf("lock %s: %v\n%s", mode, err, out)
		}
	}
}
