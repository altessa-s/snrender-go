// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"bufio"
	"bytes"
	"context"
	"fmt"
	"github.com/altessa-s/snrender-go"
	"hash/fnv"
	"image/color"
	"image/png"
	"io"
	"math"
	"os"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

const fixture = "testdata/supernote-typescript/blank-a6x-3.15.27-two-pages.note"

func read(t testing.TB, path string) []byte {
	t.Helper()
	p, e := os.ReadFile(path)
	if e != nil {
		t.Fatal(e)
	}
	return p
}
func open(t testing.TB, path string) *snrender.Notebook {
	t.Helper()
	n, e := snrender.Open(read(t, path))
	if e != nil {
		t.Fatal(e)
	}
	t.Cleanup(func() { _ = n.Close() })
	return n
}
func digest(p []byte) string {
	h := fnv.New64a()
	_, _ = h.Write(p)
	return fmt.Sprintf("%016x", h.Sum64())
}
func TestCompositionGoldens(t *testing.T) {
	scan := bufio.NewScanner(bytes.NewReader(read(t, "testdata/goldens.tsv")))
	checked := 0
	for scan.Scan() {
		f := strings.Fields(scan.Text())
		page, _ := strconv.Atoi(f[1])
		if testing.Short() && (page != 0 || (!strings.Contains(f[0], "blank-a6x") && !strings.Contains(f[0], "isolation-tools") && !strings.HasSuffix(f[0], ".mark"))) {
			continue
		}
		t.Run(f[0]+"/"+f[1], func(t *testing.T) {
			n := open(t, "testdata/"+f[0])
			o := snrender.DefaultOptions()
			o.Background = &snrender.Color{Red: 1, Green: 1, Blue: 1, Alpha: 1}
			p, e := n.Raster(context.Background(), page, &o)
			if e != nil {
				t.Fatal(e)
			}
			if f[2] != "-" {
				if digest(p.Gray) != f[2] || digest(p.Alpha) != f[3] {
					t.Fatal("raster differs from C composition golden")
				}
				checked++
			}
		})
	}
	if e := scan.Err(); e != nil {
		t.Fatal(e)
	}
	if !testing.Short() && checked != 63 {
		t.Fatalf("checked %d goldens, want 63", checked)
	}
}
func TestEveryFixtureEntryPoint(t *testing.T) {
	scan := bufio.NewScanner(bytes.NewReader(read(t, "testdata/goldens.tsv")))
	for scan.Scan() {
		f := strings.Fields(scan.Text())
		page, _ := strconv.Atoi(f[1])
		if testing.Short() && (page != 0 || (!strings.Contains(f[0], "blank-a6x") && !strings.Contains(f[0], "isolation-tools") && !strings.HasSuffix(f[0], ".mark"))) {
			continue
		}
		t.Run(f[0]+"/"+f[1], func(t *testing.T) {
			t.Parallel()
			n := open(t, "testdata/"+f[0])
			ctx := context.Background()
			o := &snrender.Options{Scale: 0.125, Background: &snrender.Color{Red: 1, Green: 1, Blue: 1, Alpha: 1}}
			g, e := n.PageGeometry(page)
			if e != nil {
				t.Fatal(e)
			}
			w, h := int(math.Ceil(g.Width*o.Scale)), int(math.Ceil(g.Height*o.Scale))
			bitmap, e := n.Bitmap(ctx, page, o)
			if e != nil {
				t.Fatal(e)
			}
			if len(bitmap.Pix) != w*h*4 {
				t.Fatal("bitmap dimensions")
			}
			encoded, e := n.PNG(ctx, page, o)
			if e != nil {
				t.Fatal(e)
			}
			img, e := png.Decode(bytes.NewReader(encoded))
			if e != nil {
				t.Fatal(e)
			}
			if img.Bounds().Dx() != w || img.Bounds().Dy() != h {
				t.Fatal("PNG dimensions")
			}
			for y := range h {
				for x := range w {
					pixel := color.NRGBAModel.Convert(img.At(x, y)).(color.NRGBA)
					at := (y*w + x) * 4
					if pixel.A != bitmap.Pix[at+3] {
						t.Fatalf("PNG alpha differs at %d,%d", x, y)
					}
					for c, v := range []byte{pixel.R, pixel.G, pixel.B} {
						if byte(uint32(v)*uint32(pixel.A)/255) != bitmap.Pix[at+c] {
							t.Fatalf("PNG pixel differs at %d,%d channel %d", x, y, c)
						}
					}
				}
			}

			methods := []struct {
				name   string
				buffer func(context.Context, int, *snrender.Options) ([]byte, error)
				stream func(context.Context, io.Writer, int, *snrender.Options) error
			}{
				{"PNG", n.PNG, n.WritePNG}, {"SVG", n.SVG, n.WriteSVG}, {"PDF", n.PagePDF, n.WritePagePDF},
			}
			for _, m := range methods {
				p, e := m.buffer(ctx, page, o)
				if e != nil {
					t.Fatalf("%s: %v", m.name, e)
				}
				var out bytes.Buffer
				if e = m.stream(ctx, &out, page, o); e != nil {
					t.Fatalf("%s: %v", m.name, e)
				}
				if !sameOutput(p, out.Bytes(), m.name == "PDF") {
					t.Fatalf("%s streaming differs", m.name)
				}
			}
			p, e := n.PDF(ctx, page, 1, o)
			if e != nil {
				t.Fatal(e)
			}
			var out bytes.Buffer
			if e = n.WritePDF(ctx, &out, page, 1, o); e != nil {
				t.Fatal(e)
			}
			if !sameOutput(p, out.Bytes(), true) {
				t.Fatal("document streaming differs")
			}
		})
	}
	if e := scan.Err(); e != nil {
		t.Fatal(e)
	}
}

// Native PDF encoders generate export identities and wall-clock metadata.
// Normalize only those fields; content streams and object layout stay exact.
func sameOutput(a, b []byte, pdf bool) bool {
	if !pdf {
		return bytes.Equal(a, b)
	}
	for _, pattern := range []string{`/ID\s*\[\s*<[0-9a-fA-F]+>\s*<[0-9a-fA-F]+>\s*\]`, `/(CreationDate|ModDate)\s*\([^)]*\)`} {
		r := regexp.MustCompile(pattern)
		a = r.ReplaceAll(a, []byte("volatile"))
		b = r.ReplaceAll(b, []byte("volatile"))
	}
	return bytes.Equal(a, b)
}
