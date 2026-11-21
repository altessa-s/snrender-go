// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"context"
	"errors"
	"io"
	"sync"
	"testing"
	"time"

	"github.com/altessa-s/snrender-go"
)

func TestCloseFromCallback(t *testing.T) {
	n := open(t, fixture)
	done := make(chan error, 1)
	go func() {
		done <- n.WritePNG(context.Background(), writerFunc(func(p []byte) (int, error) { _ = n.Close(); return len(p), nil }), 0, &snrender.Options{Scale: .125})
	}()
	select {
	case err := <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(time.Second):
		t.Fatal("Close blocked in callback")
	}
}

func TestRenderFromCallback(t *testing.T) {
	for _, closed := range []bool{false, true} {
		n := open(t, fixture)
		done := make(chan error, 1)
		go func() {
			var once sync.Once
			done <- n.WritePNG(context.Background(), writerFunc(func(p []byte) (int, error) {
				once.Do(func() {
					if closed {
						ch := make(chan struct{})
						go func() { _ = n.Close(); close(ch) }()
						<-ch
					}
					_, err := n.PNG(context.Background(), 1, &snrender.Options{Scale: .125})
					if closed {
						if !errors.Is(err, snrender.ErrClosed) {
							t.Error(err)
						}
					} else if err != nil {
						t.Error(err)
					}
				})
				return len(p), nil
			}), 0, &snrender.Options{Scale: .125})
		}()
		select {
		case err := <-done:
			if err != nil {
				t.Fatal(err)
			}
		case <-time.After(time.Second):
			t.Fatal("nested call blocked")
		}
	}
}

func TestCloseRacingRenders(t *testing.T) {
	for range 10 {
		n := open(t, fixture)
		var wg sync.WaitGroup
		start := make(chan struct{})
		for range 32 {
			wg.Add(1)
			go func() {
				defer wg.Done()
				<-start
				_, err := n.PNG(context.Background(), 0, &snrender.Options{Scale: .125})
				if err != nil && !errors.Is(err, snrender.ErrClosed) {
					t.Error(err)
				}
			}()
		}
		close(start)
		_ = n.Close()
		wg.Wait()
	}
}

func TestFailingCallsReleaseNotebook(t *testing.T) {
	before := snrender.LiveNotebooks()
	n := open(t, fixture)
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	o := &snrender.Options{Scale: .125}
	for range 100 {
		_, _ = n.Bitmap(ctx, 0, o)
		_, _ = n.Raster(ctx, 0, o)
		for _, m := range []func(context.Context, int, *snrender.Options) ([]byte, error){n.PNG, n.SVG, n.PagePDF} {
			_, _ = m(ctx, 0, o)
		}
		_, _ = n.PDF(ctx, 0, snrender.AllPages, o)
		for _, m := range []func(context.Context, io.Writer, int, *snrender.Options) error{n.WritePNG, n.WriteSVG, n.WritePagePDF} {
			_ = m(ctx, io.Discard, 0, o)
		}
		_ = n.WritePDF(ctx, io.Discard, 0, snrender.AllPages, o)
		_, _ = n.PNG(context.Background(), 99, o)
		_, _ = n.PNG(context.Background(), 0, &snrender.Options{Limits: snrender.Limits{MaximumPixels: -1}})
	}
	_ = n.Close()
	if got := snrender.LiveNotebooks(); got != before {
		t.Fatalf("live C notebooks %d, want %d", got, before)
	}
}
