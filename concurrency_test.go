// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"bytes"
	"context"
	"errors"
	"github.com/altessa-s/snrender-go"
	"sync"
	"testing"
	"time"
)

func TestConcurrentRendering(t *testing.T) {
	n := open(t, fixture)
	o := &snrender.Options{Scale: .125}
	ctx := context.Background()
	want, e := n.PNG(ctx, 0, o)
	if e != nil {
		t.Fatal(e)
	}
	var wg sync.WaitGroup
	for range 16 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for range 3 {
				p, e := n.PNG(ctx, 0, o)
				if e != nil || !bytes.Equal(p, want) {
					t.Error("concurrent output", e)
				}
			}
		}()
	}
	wg.Wait()
}
func TestCloseDefersRelease(t *testing.T) {
	n := open(t, fixture)
	entered, release := make(chan struct{}), make(chan struct{})
	done := make(chan error, 1)
	var once sync.Once
	go func() {
		done <- n.WritePNG(context.Background(), writerFunc(func(p []byte) (int, error) { once.Do(func() { close(entered); <-release }); return len(p), nil }), 0, &snrender.Options{Scale: .125})
	}()
	<-entered
	closed := make(chan struct{})
	go func() { _ = n.Close(); close(closed) }()
	select {
	case <-closed:

	case <-time.After(time.Second):
		t.Fatal("Close blocked during render")
	}
	close(release)
	if e := <-done; e != nil {
		t.Fatal(e)
	}
	<-closed
	if _, e := n.PNG(context.Background(), 0, nil); !errors.Is(e, snrender.ErrClosed) {
		t.Fatal(e)
	}
}
