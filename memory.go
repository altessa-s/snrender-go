// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

//go:build cgo && (darwin || linux || windows)

package snrender

import "unsafe"

// copyBytes avoids C.GoBytes' narrower C int length and owns its result.
func copyBytes(p unsafe.Pointer, n uint64) ([]byte, error) {
	if n > uint64(^uint(0)>>1) {
		return nil, &Error{Status: StatusOutOfMemory, Message: "result exceeds Go slice capacity"}
	}
	return append([]byte(nil), unsafe.Slice((*byte)(p), int(n))...), nil
}

// callbackRunner reuses one goroutine for a call's cancellation, progress and output callbacks.
// Waiting for each answer keeps caller code synchronous without allowing it
// to unwind a C frame. An abnormal answer ends the worker as well.
type callbackRunner struct {
	jobs    chan func()
	answers chan callbackResult
	stopped chan struct{}
}

type callbackResult struct {
	returned   bool
	panicValue any
}

func (r *callbackRunner) run(fn func()) (bool, any) {
	if r.jobs == nil {
		r.jobs = make(chan func())
		r.answers = make(chan callbackResult)
		r.stopped = make(chan struct{})
		go func() {
			defer close(r.stopped)
			finished := false
			defer func() {
				if !finished {
					r.answers <- callbackResult{panicValue: recover()}
				}
			}()
			for fn := range r.jobs {
				fn()
				// Drop the closure before answering so borrowed data is not kept
				// while the worker waits for its next piece.
				fn = nil //nolint:ineffassign // Release borrowed captures before waiting for another job.
				r.answers <- callbackResult{returned: true}
			}
			finished = true
		}()
	}
	r.jobs <- fn
	answer := <-r.answers
	return answer.returned, answer.panicValue
}

func (r *callbackRunner) stop() {
	if r.jobs != nil {
		close(r.jobs)
		<-r.stopped
	}
}
