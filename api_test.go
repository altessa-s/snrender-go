// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"context"
	"errors"
	"strings"
	"sync/atomic"
	"testing"

	"github.com/altessa-s/snrender-go"
)

type observedContext struct {
	context.Context
	doneCalls, errCalls atomic.Int64
}

func (c *observedContext) Done() <-chan struct{} { c.doneCalls.Add(1); return c.Context.Done() }
func (c *observedContext) Err() error            { c.errCalls.Add(1); return c.Context.Err() }

func TestCancellationQueries(t *testing.T) {
	t.Parallel()
	n := open(t, fixture)
	for _, mode := range []string{"background", "cancellable", "cancelled"} {
		t.Run(mode, func(t *testing.T) {
			ctx := context.Background()
			if mode != "background" {
				var cancel context.CancelFunc
				ctx, cancel = context.WithCancel(ctx)
				defer cancel()
				if mode == "cancelled" {
					cancel()
				}
			}
			c := &observedContext{Context: ctx}
			_, err := n.PDF(c, 0, snrender.AllPages, &snrender.Options{Scale: .125})
			wantErrCalls := int64(0)
			if mode == "cancelled" {
				wantErrCalls = 1
				if !errors.Is(err, snrender.ErrCancelled) || !errors.Is(err, context.Canceled) {
					t.Fatal(err)
				}
			} else if err != nil {
				t.Fatal(err)
			}
			if c.doneCalls.Load() != 1 || c.errCalls.Load() != wantErrCalls {
				t.Fatalf("Done=%d Err=%d", c.doneCalls.Load(), c.errCalls.Load())
			}
		})
	}
}

func TestRasterPlaneOwnership(t *testing.T) {
	t.Parallel()
	n := open(t, fixture)
	r, err := n.Raster(context.Background(), 0, &snrender.Options{Scale: .125})
	if err != nil {
		t.Fatal(err)
	}
	_ = n.Close()
	gray, alpha := r.GrayImage(), r.AlphaImage()
	r.Gray[0] = 42
	r.Alpha[0] = 17
	if gray.Pix[0] != 42 || alpha.Pix[0] != 17 || &r.Gray[0] == &r.Alpha[0] {
		t.Fatal("planes are not independently owned and shared with image helpers")
	}
}

func TestErrorDiagnostics(t *testing.T) {
	t.Parallel()
	for _, err := range []error{snrender.ErrInvalidArgument, snrender.ErrOutOfMemory, snrender.ErrLimit, snrender.ErrUnsupported, snrender.ErrBackend, snrender.ErrCancelled, snrender.ErrParseInvalidArgument, snrender.ErrParseOutOfMemory, snrender.ErrUnsupportedFormat, snrender.ErrUnexpectedEOF, snrender.ErrMalformed, snrender.ErrImageEncoding, snrender.ErrUnsupportedEncoding, snrender.ErrParseLimit} {
		if strings.HasSuffix(err.Error(), ": ") {
			t.Fatal(err)
		}
	}
	for s := snrender.ParseOK; s <= snrender.ParseLimit; s++ {
		e := &snrender.ParseError{Status: s, Message: "detail"}
		if s.String() == "" || !strings.Contains(e.Error(), s.String()) {
			t.Fatal(e)
		}
	}
	n := open(t, fixture)
	for _, tc := range []struct {
		first, count int
		argument     string
	}{{-1, 1, "first"}, {2, snrender.AllPages, "first"}, {0, 0, "count"}, {0, -2, "count"}, {1, 2, "count"}} {
		_, err := n.PDF(context.Background(), tc.first, tc.count, nil)
		if !errors.Is(err, snrender.ErrInvalidArgument) || !strings.Contains(err.Error(), tc.argument) {
			t.Fatalf("%+v: %v", tc, err)
		}
	}
}
