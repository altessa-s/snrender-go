// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"context"
	"errors"
	"github.com/altessa-s/snrender-go"
	"io"
	"math"
	"testing"
	"time"
)

type writerFunc func([]byte) (int, error)

func (f writerFunc) Write(p []byte) (int, error) { return f(p) }
func TestLimitsAndOptions(t *testing.T) {
	n := open(t, fixture)
	ctx := context.Background()
	tests := []struct {
		name   string
		change func(*snrender.Limits)
		want   error
	}{
		{"pixels", func(l *snrender.Limits) { l.MaximumPixels = 1 }, snrender.ErrLimit},
		{"decoded", func(l *snrender.Limits) { l.MaximumDecodedPixels = 1 }, snrender.ErrLimit},
		{"pages", func(l *snrender.Limits) { l.MaximumPages = 1 }, snrender.ErrLimit},
		{"work", func(l *snrender.Limits) { l.MaximumWork = 1 }, snrender.ErrLimit},
		{"document work", func(l *snrender.Limits) { l.MaximumDocumentWork = 1 }, snrender.ErrLimit},
		{"output", func(l *snrender.Limits) { l.MaximumOutputBytes = 1 }, snrender.ErrLimit},
		{"minimum scale", func(l *snrender.Limits) { l.MinimumScale = 2 }, snrender.ErrLimit},
		{"maximum scale", func(l *snrender.Limits) { l.MaximumScale = 0.5 }, snrender.ErrLimit},
		{"negative", func(l *snrender.Limits) { l.MaximumPixels = -1 }, snrender.ErrInvalidArgument},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			l := snrender.DefaultLimits()
			tc.change(&l)
			_, e := n.PDF(ctx, 0, snrender.AllPages, &snrender.Options{Limits: l})
			if !errors.Is(e, tc.want) {
				t.Fatalf("%v, want %v", e, tc.want)
			}
		})
	}
	for _, o := range []*snrender.Options{{Scale: -1}, {Scale: math.NaN()}, {Scale: math.Inf(1)}, {Title: "a\x00b"}, {Background: &snrender.Color{Alpha: 2}}} {
		if _, e := n.PNG(ctx, 0, o); !errors.Is(e, snrender.ErrInvalidArgument) {
			t.Fatal(e)
		}
	}
	o := snrender.DefaultOptions()
	if o.Limits != snrender.DefaultLimits() {
		t.Fatal("default limits differ")
	}
	a, e := n.PNG(ctx, 0, nil)
	if e != nil {
		t.Fatal(e)
	}
	b, e := n.PNG(ctx, 0, &o)
	if e != nil || string(a) != string(b) {
		t.Fatal("defaults differ", e)
	}
}
func TestErrorsAndLifetime(t *testing.T) {
	data := read(t, fixture)
	n, e := snrender.Open(data)
	if e != nil {
		t.Fatal(e)
	}
	clear(data)
	ctx := context.Background()
	if _, e = n.PNG(ctx, 0, &snrender.Options{Scale: .125}); e != nil {
		t.Fatal("input retained", e)
	}
	for _, page := range []int{-1, 99} {
		if _, e = n.PNG(ctx, page, nil); !errors.Is(e, snrender.ErrInvalidArgument) {
			t.Fatal(e)
		}
	}
	if _, e = n.PNG(nil, 0, nil); !errors.Is(e, snrender.ErrInvalidArgument) { //nolint:staticcheck // Exercise invalid context validation.
		t.Fatal(e)
	}
	if e = n.WritePNG(ctx, nil, 0, nil); !errors.Is(e, snrender.ErrInvalidArgument) {
		t.Fatal(e)
	}
	for _, count := range []int{0, -2, 99} {
		if _, e = n.PDF(ctx, 0, count, nil); !errors.Is(e, snrender.ErrInvalidArgument) {
			t.Fatal(e)
		}
	}
	cancelled, cancel := context.WithCancel(ctx)
	cancel()
	if _, e = n.PNG(cancelled, 0, nil); !errors.Is(e, context.Canceled) || !errors.Is(e, snrender.ErrCancelled) {
		t.Fatal(e)
	}
	if e = n.Close(); e != nil {
		t.Fatal(e)
	}
	if e = n.Close(); e != nil {
		t.Fatal(e)
	}
	if _, e = n.PNG(ctx, 0, nil); !errors.Is(e, snrender.ErrClosed) {
		t.Fatal(e)
	}
	if _, e = n.PageCount(); !errors.Is(e, snrender.ErrClosed) {
		t.Fatal(e)
	}
	if _, e = n.PageGeometry(0); !errors.Is(e, snrender.ErrClosed) {
		t.Fatal(e)
	}
	var zero snrender.Notebook
	if _, e = zero.PNG(ctx, 0, nil); !errors.Is(e, snrender.ErrClosed) {
		t.Fatal(e)
	}
}
func TestWriterFailure(t *testing.T) {
	n := open(t, fixture)
	ctx := context.Background()
	refused := errors.New("refused")
	for _, method := range []func(context.Context, io.Writer, int, *snrender.Options) error{n.WritePNG, n.WriteSVG, n.WritePagePDF} {
		calls := 0
		e := method(ctx, writerFunc(func([]byte) (int, error) { calls++; return 0, refused }), 0, &snrender.Options{Scale: .125})
		if e != refused || calls != 1 { //nolint:errorlint // The contract preserves error identity, not only errors.Is.
			t.Fatalf("%v, calls %d", e, calls)
		}
		e = method(ctx, writerFunc(func(p []byte) (int, error) { return len(p) - 1, nil }), 0, &snrender.Options{Scale: .125})
		if !errors.Is(e, io.ErrShortWrite) {
			t.Fatal(e)
		}
	}
	// A preflight limit is earlier than any refusal during PDF finalization.
	l := snrender.DefaultLimits()
	l.MaximumWork = 1
	e := n.WritePDF(ctx, writerFunc(func([]byte) (int, error) { return 0, refused }), 0, 1, &snrender.Options{Limits: l})
	if !errors.Is(e, snrender.ErrLimit) {
		t.Fatal(e)
	}
}

func TestCancellationPrecedesClosingRefusal(t *testing.T) {
	n := open(t, fixture)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	refused := errors.New("closing writer refusal")
	o := &snrender.Options{Scale: .125, Progress: func(completed, _ int) {
		if completed == 1 {
			cancel()
		}
	}}
	err := n.WritePDF(ctx, writerFunc(func(p []byte) (int, error) {
		if ctx.Err() != nil {
			return 0, refused
		}
		return len(p), nil
	}), 0, snrender.AllPages, o)
	if !errors.Is(err, context.Canceled) || errors.Is(err, refused) {
		t.Fatalf("first cancellation was lost: %v", err)
	}
}
func TestVersionAndProgress(t *testing.T) {
	v := snrender.Version()
	if v.Binding != "1.0.0" || v.Library != "1.0.0" || v.RendererCommit != "032b7048d29617c8b9861256b59c3cf9a7bf578f" || v.ReaderCommit != "2cab83674ba017abe0e0e42eb5b6e5558a27155f" || v.DrawingReaderCommit != "0ac7ba43dcf4b798d9ac5c845dc2eee2b7d45b3b" || snrender.LibraryVersion != "v1.0.0" {
		t.Fatalf("version %+v", v)
	}
	n := open(t, fixture)
	completed := 0
	o := &snrender.Options{Scale: .125, Progress: func(done, total int) {
		if done != completed || total != 2 {
			t.Errorf("progress %d/%d, expected %d/2", done, total, completed)
		}
		completed++
	}}
	if _, e := n.PDF(context.Background(), 0, snrender.AllPages, o); e != nil {
		t.Fatal(e)
	}
	if completed != 3 {
		t.Fatalf("progress calls %d", completed)
	}
}

func TestErrorStatuses(t *testing.T) {
	for status, sentinel := range map[snrender.Status]error{
		snrender.StatusInvalidArgument: snrender.ErrInvalidArgument,
		snrender.StatusOutOfMemory:     snrender.ErrOutOfMemory,
		snrender.StatusLimit:           snrender.ErrLimit,
		snrender.StatusUnsupported:     snrender.ErrUnsupported,
		snrender.StatusBackend:         snrender.ErrBackend,
		snrender.StatusCancelled:       snrender.ErrCancelled,
	} {
		e := &snrender.Error{Status: status, Index: 3, Message: "diagnostic"}
		if !errors.Is(e, sentinel) || status.String() == "unknown status" || e.Error() == "" {
			t.Fatal(e)
		}
	}
	for status, sentinel := range map[snrender.ParseStatus]error{
		snrender.ParseInvalidArgument:     snrender.ErrParseInvalidArgument,
		snrender.ParseOutOfMemory:         snrender.ErrParseOutOfMemory,
		snrender.ParseUnsupportedFormat:   snrender.ErrUnsupportedFormat,
		snrender.ParseUnexpectedEOF:       snrender.ErrUnexpectedEOF,
		snrender.ParseMalformed:           snrender.ErrMalformed,
		snrender.ParseImageEncoding:       snrender.ErrImageEncoding,
		snrender.ParseUnsupportedEncoding: snrender.ErrUnsupportedEncoding,
		snrender.ParseLimit:               snrender.ErrParseLimit,
	} {
		if !errors.Is(&snrender.ParseError{Status: status}, sentinel) {
			t.Fatal(status)
		}
	}
	if snrender.Status(^uint32(0)).String() != "unknown status" {
		t.Fatal("unknown status truncated")
	}
}

func TestDeadlineExceeded(t *testing.T) {
	n := open(t, fixture)
	ctx, cancel := context.WithDeadline(context.Background(), time.Unix(0, 0))
	defer cancel()
	if _, e := n.PNG(ctx, 0, nil); !errors.Is(e, context.DeadlineExceeded) || !errors.Is(e, snrender.ErrCancelled) {
		t.Fatal(e)
	}
}
