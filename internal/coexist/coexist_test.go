// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.
package coexist_test

import (
	"bytes"
	"context"
	_ "github.com/altessa-s/rmrender-go"
	"github.com/altessa-s/snnote-go"
	"github.com/altessa-s/snrender-go"
	host "github.com/altessa-s/snrender-go/internal/coexist"
	"github.com/altessa-s/snspd-go"
	"os"
	"sync"
	"testing"
)

func TestMarkOverlayAndReader(t *testing.T) {
	data, err := os.ReadFile("../../testdata/samples/pdf-highlights-n6-20260016.mark")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := snnote.Parse(data); err != nil {
		t.Fatal(err)
	}
	n, err := snrender.Open(data)
	if err != nil {
		t.Fatal(err)
	}
	defer n.Close()
	source, err := os.ReadFile("../../testdata/overlay/mixed.pdf")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := n.OverlayPNG(context.Background(), source, 0, nil); err != nil {
		t.Fatal(err)
	}
	result, err := n.OverlayPDF(context.Background(), source, 0, snrender.AllPages, nil)
	if err != nil || !bytes.HasPrefix(result, source) {
		t.Fatalf("overlay beside reader: %v", err)
	}
	if _, err := snnote.Parse(data); err != nil {
		t.Fatal(err)
	}
}

func TestReaderAndRenderer(t *testing.T) {
	data, e := os.ReadFile("../../testdata/supernote-typescript/blank-a6x-3.15.27-two-pages.note")
	if e != nil {
		t.Fatal(e)
	}
	reader, e := snnote.Parse(data)
	if e != nil {
		t.Fatal(e)
	}
	renderer, e := snrender.Open(data)
	if e != nil {
		t.Fatal(e)
	}
	defer renderer.Close()
	count, e := renderer.PageCount()
	if e != nil || count != len(reader.Pages) {
		t.Fatal("page counts differ", e)
	}
	png, e := renderer.PNG(context.Background(), 0, &snrender.Options{Scale: .125})
	if e != nil || len(png) == 0 {
		t.Fatal(e)
	}
	if _, e = snnote.Parse(data); e != nil {
		t.Fatal(e)
	}
}

func TestDrawingReadersAndHostSQLite(t *testing.T) {
	h, err := host.OpenHost()
	if err != nil {
		t.Fatal(err)
	}
	defer h.Close()
	if err := h.Exec("CREATE TABLE retained(n); INSERT INTO retained VALUES(42)"); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile("../../testdata/spd/valid.spd")
	if err != nil {
		t.Fatal(err)
	}
	var wg sync.WaitGroup
	for range 4 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for range 5 {
				d, err := snspd.Open(data, nil)
				if err != nil || d == nil || len(d.Layers) != 3 {
					t.Errorf("snspd-go: %v", err)
					return
				}
				if _, err := snrender.DrawingPNG(context.Background(), data, &snrender.DrawingOptions{Scale: .125}); err != nil {
					t.Error(err)
					return
				}
			}
		}()
	}
	for range 10 {
		if n, err := h.Value(); err != nil || n != 42 {
			t.Fatalf("host SQLite: %d %v", n, err)
		}
	}
	wg.Wait()
	if err := h.Exec("UPDATE retained SET n=43"); err != nil {
		t.Fatal(err)
	}
	if n, err := h.Value(); err != nil || n != 43 {
		t.Fatalf("host SQLite after readers: %d %v", n, err)
	}
}
