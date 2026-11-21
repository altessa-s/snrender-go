// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

/*
#include "bridge.h"
*/
import "C"
import (
	"context"
	"io"
	"runtime"
	"runtime/cgo"
	"sync/atomic"
	"unsafe"
)

// call carries only an integer handle through C. Caller code runs away from
// C frames; abnormal completion is raised only after owned results are freed.
type call struct {
	ctx                   context.Context
	done                  <-chan struct{}
	cancelled             bool
	writer                io.Writer
	writeBuffer           []byte
	progress              func(int, int)
	worker                callbackRunner
	handle                cgo.Handle
	writerErr, contextErr error
	panicked, exited      bool
	panicValue            any
}

var liveHandles atomic.Int64

func newCall(ctx context.Context, w io.Writer, progress func(int, int)) *call {
	c := &call{ctx: ctx, done: ctx.Done(), writer: w, progress: progress}
	c.handle = cgo.NewHandle(c)
	liveHandles.Add(1)
	return c
}
func (c *call) invoke(fn func()) bool {
	if c.panicked || c.exited {
		return false
	}
	returned, p := c.worker.run(fn)
	c.panicked, c.panicValue, c.exited = p != nil, p, !returned && p == nil
	return returned
}
func (c *call) finish() {
	c.handle.Delete()
	c.worker.stop()
	liveHandles.Add(-1)
	if c.panicked {
		panic(c.panicValue)
	}
	if c.exited {
		runtime.Goexit()
	}
}

//export snrgoCancel
func snrgoCancel(handle C.uintptr_t) C.bool {
	c := cgo.Handle(handle).Value().(*call)
	if c.cancelled || c.panicked || c.exited {
		return true
	}
	select {
	case <-c.done:
		c.cancelled = true
		c.invoke(func() { c.contextErr = c.ctx.Err() })
	default:
		return false
	}
	return C.bool(c.cancelled || c.panicked || c.exited)
}

//export snrgoProgress
func snrgoProgress(handle C.uintptr_t, completed, total C.size_t) {
	c := cgo.Handle(handle).Value().(*call)
	if c.progress != nil {
		c.invoke(func() { c.progress(int(completed), int(total)) })
	}
}

// The bridge supplies at most 64 KiB per callback. The writer receives a
// Go-owned copy, reused only after Write returns.
// As required by io.Writer, the writer must not retain the slice.
func (c *call) write(p []byte) bool {
	if c.writerErr != nil || c.panicked || c.exited {
		return false
	}
	if len(p) == 0 {
		return true
	}
	c.writeBuffer = append(c.writeBuffer[:0], p...)
	piece := c.writeBuffer
	if !c.invoke(func() {
		written, err := c.writer.Write(piece)
		if err != nil {
			c.writerErr = err
		} else if written != len(piece) {
			c.writerErr = io.ErrShortWrite
		}
	}) || c.writerErr != nil {
		return false
	}
	return true
}

//export snrgoWrite
func snrgoWrite(handle C.uintptr_t, p *C.uint8_t, n C.size_t) C.snr_status {
	c := cgo.Handle(handle).Value().(*call)
	if uint64(n) > uint64(^uint(0)>>1) {
		c.writerErr = ErrOutOfMemory
		return C.SNRGO_REFUSED
	}
	if !c.write(unsafe.Slice((*byte)(unsafe.Pointer(p)), int(n))) {
		return C.SNRGO_REFUSED
	}
	return C.SNR_OK
}

func (c *call) resultError(status C.snr_status, e *C.snr_error) error {
	if status == C.SNRGO_REFUSED && c.writerErr != nil {
		return c.writerErr
	}
	if status != C.SNR_OK {
		result := cError(status, e)
		if status == C.SNR_ERR_CANCELLED {
			result.Cause = c.contextErr
		}
		return result
	}
	if c.panicked || c.exited {
		return ErrCancelled
	}
	return nil
}
