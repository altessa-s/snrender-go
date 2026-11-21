// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"context"
	"github.com/altessa-s/snrender-go"
	"runtime"
	"sync"
	"sync/atomic"
	"testing"
)

func TestLogAbnormalCompletion(t *testing.T) {
	for _, panics := range []bool{false, true} {
		var calls atomic.Int64
		snrender.SetLogger(func(snrender.LogLevel, string) {
			calls.Add(1)
			if panics {
				panic("log")
			}
			runtime.Goexit()
		})
		n, e := snrender.Open(damaged())
		if e != nil {
			t.Fatal(e)
		}
		_ = n.Close()
		n, e = snrender.Open(imageNotebook("PNG", []byte("bad")))
		if e != nil {
			t.Fatal(e)
		}
		if _, e = n.PNG(context.Background(), 0, &snrender.Options{Scale: .125}); e != nil {
			t.Fatal(e)
		}
		_ = n.Close()
		snrender.SetLogger(nil)
		if calls.Load() < 2 {
			t.Fatal("both library log bridges must run")
		}
	}
}
func TestConcurrentLogReplacement(t *testing.T) {
	defer snrender.SetLogger(nil)
	var calls atomic.Int64
	var wg sync.WaitGroup
	for range 8 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for range 20 {
				snrender.SetLogger(func(snrender.LogLevel, string) { calls.Add(1) })
				n, e := snrender.Open(damaged())
				if e != nil {
					t.Error(e)
				} else {
					_ = n.Close()
				}
				snrender.SetLogger(nil)
			}
		}()
	}
	wg.Wait()
}
