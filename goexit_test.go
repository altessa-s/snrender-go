// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"bytes"
	"context"
	"fmt"
	"github.com/altessa-s/snrender-go"
	"os"
	"os/exec"
	"runtime"
	"sync"
	"testing"
)

type callbackContext struct {
	context.Context
	fn func()
}

func (c callbackContext) Done() <-chan struct{} { ch := make(chan struct{}); close(ch); return ch }
func (c callbackContext) Err() error            { c.fn(); return nil }
func TestAbnormalCallbacks(t *testing.T) {
	n := open(t, fixture)
	before := snrender.LiveHandles()
	for _, kind := range []string{"writer", "cancel", "progress"} {
		for _, panics := range []bool{false, true} {
			t.Run(kind+map[bool]string{false: "Exit", true: "Panic"}[panics], func(t *testing.T) {
				var wg sync.WaitGroup
				for range 20 {
					wg.Add(1)
					go func() {
						defer wg.Done()
						defer func() {
							p := recover()
							if panics && p != "stop" {
								t.Errorf("panic = %v", p)
							}
							if !panics && p != nil {
								t.Errorf("unexpected panic %v", p)
							}
						}()
						fn := func() {
							if panics {
								panic("stop")
							}
							runtime.Goexit()
						}
						o := &snrender.Options{Scale: .125}
						switch kind {
						case "writer":
							_ = n.WritePNG(context.Background(), writerFunc(func(p []byte) (int, error) { fn(); return len(p), nil }), 0, o)
						case "cancel":
							_, _ = n.PNG(callbackContext{context.Background(), fn}, 0, o)
						case "progress":
							o.Progress = func(int, int) { fn() }
							_, _ = n.PDF(context.Background(), 0, 1, o)
						}
						t.Error("abnormal callback returned")
					}()
				}
				wg.Wait()
				if got := snrender.LiveHandles(); got != before {
					t.Fatalf("handles %d, want %d", got, before)
				}
				// Abnormal callbacks must not leave the notebook locked or unusable.
				if _, e := n.PNG(context.Background(), 0, &snrender.Options{Scale: .125}); e != nil {
					t.Fatal(e)
				}
			})
		}
	}
}

func TestSecondWriterExit(t *testing.T) {
	n := streamingNotebook(t)
	before := snrender.LiveHandles()
	calls := 0
	done := make(chan struct{})
	go func() {
		defer close(done)
		_ = n.WriteSVG(context.Background(), writerFunc(func(p []byte) (int, error) {
			calls++
			if calls == 2 {
				runtime.Goexit()
			}
			return len(p), nil
		}), 0, &snrender.Options{Scale: .5})
		t.Error("returned after exit")
	}()
	<-done
	if calls != 2 || snrender.LiveHandles() != before {
		t.Fatalf("calls %d, handles %d", calls, snrender.LiveHandles())
	}
}

// A child process lets Fatal fail a subtest while the outer test checks cleanup.
func TestCallbackFatal(t *testing.T) {
	if os.Getenv("SNRENDER_CALLBACK_FATAL") == "1" {
		n := open(t, fixture)
		before := snrender.LiveHandles()
		deferred := false
		ok := t.Run("callback", func(t *testing.T) {
			defer func() { deferred = true }()
			_ = n.WritePNG(context.Background(), writerFunc(func(p []byte) (int, error) { t.Fatal("callback fatal"); return len(p), nil }), 0, &snrender.Options{Scale: .125})
		})
		if !ok && deferred && snrender.LiveHandles() == before {
			fmt.Println("fatal cleaned up")
		}
		return
	}
	cmd := exec.Command(os.Args[0], "-test.run=^TestCallbackFatal$")
	cmd.Env = append(os.Environ(), "SNRENDER_CALLBACK_FATAL=1")
	out, e := cmd.CombinedOutput()
	if e == nil || !bytes.Contains(out, []byte("fatal cleaned up")) {
		t.Fatalf("child %v: %s", e, out)
	}
}
