// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"github.com/altessa-s/snrender-go"
	"testing"
)

func block(data, p []byte) []byte {
	return append(binary.LittleEndian.AppendUint32(data, uint32(len(p))), p...)
}
func begin() []byte { return block([]byte("noteSN_FILE_VER_20230015"), []byte("<APPLY_EQUIPMENT:N5>")) }
func finish(data []byte, footer string) []byte {
	at := len(data)
	data = block(data, []byte(footer+"<FILE_FEATURE:24>"))
	data = append(data, []byte("tail")...)
	return binary.LittleEndian.AppendUint32(data, uint32(at))
}
func damaged() []byte { return finish(begin(), "<PAGE1:999999>") }
func imageNotebook(protocol string, payload []byte) []byte {
	d := begin()
	bitmap := len(d)
	d = block(d, payload)
	layer := len(d)
	d = block(d, []byte(fmt.Sprintf("<LAYERBITMAP:%d><LAYERPROTOCOL:%s>", bitmap, protocol)))
	page := len(d)
	d = block(d, []byte(fmt.Sprintf("<PAGEID:p><MAINLAYER:%d>", layer)))
	return finish(d, fmt.Sprintf("<PAGE1:%d>", page))
}
func TestHostileInputs(t *testing.T) {
	data := read(t, fixture)
	for _, size := range []int{0, 1, 4, 20, len(data) / 2, len(data) - 1} {
		n, e := snrender.Open(data[:size])
		if e == nil {
			_ = n.Close()
			t.Fatalf("truncation %d accepted", size)
		}
	}
	for at := range 22 {
		p := append([]byte(nil), data...)
		p[at] = 0xff
		n, e := snrender.Open(p)
		if e == nil {
			_ = n.Close()
			t.Fatalf("bad signature %d accepted", at)
		}
	}
	// Optional mutations may be recovered by the reader; any surviving page
	// must still be safe to render under bounded budgets.
	for at := 22; at < len(data); at += max(1, len(data)/32) {
		p := append([]byte(nil), data...)
		p[at] ^= 0xff
		if n, e := snrender.Open(p); e == nil {
			count, err := n.PageCount()
			if err != nil {
				t.Fatal(err)
			}
			exerciseRender(context.Background(), t, n, 0, count, boundedOptions(), 2)
			_ = n.Close()
		} else {
			var pe *snrender.ParseError
			if !errors.As(e, &pe) || pe.Status <= snrender.ParseOK || pe.Status > snrender.ParseLimit {
				t.Fatal(e)
			}
		}
	}
}
func TestStrictDamagedImages(t *testing.T) {
	n, e := snrender.Open(imageNotebook("PNG", []byte("not a png")))
	if e != nil {
		t.Fatal(e)
	}
	defer n.Close()
	count := 0
	snrender.SetLogger(func(snrender.LogLevel, string) { count++ })
	defer snrender.SetLogger(nil)
	o := &snrender.Options{Scale: .125}
	if _, e = n.PNG(context.Background(), 0, o); e != nil {
		t.Fatal(e)
	}
	if count == 0 {
		t.Fatal("damaged image was not logged")
	}
	o.StrictImages = true
	if _, e = n.PNG(context.Background(), 0, o); !errors.Is(e, snrender.ErrBackend) {
		t.Fatalf("strict damaged image: %v", e)
	}
}

func TestUnsupportedLayer(t *testing.T) {
	n, e := snrender.Open(imageNotebook("UNKNOWN_PROTOCOL", []byte{1, 2, 3}))
	if e != nil {
		t.Fatal(e)
	}
	defer n.Close()
	if _, e = n.PNG(context.Background(), 0, &snrender.Options{Scale: .125}); !errors.Is(e, snrender.ErrUnsupported) {
		t.Fatalf("unknown encoding: %v", e)
	}
}
