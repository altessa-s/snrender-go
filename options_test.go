// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

import (
	"reflect"
	"strings"
	"testing"
)

func TestOptionsRoundTrip(t *testing.T) {
	o := DefaultOptions()
	o.Scale = .5
	o.HideBackground = true
	o.DisableRecognition = true
	o.StrictImages = true
	o.Background = &Color{.1, .2, .3, .4}
	l := &o.Limits
	l.MaximumPixels = 123
	l.MaximumDecodedPixels = 456
	l.MaximumPages = 7
	l.MaximumWork = 8910
	l.MaximumDocumentWork = 12345
	l.MaximumOutputBytes = 6789
	l.MinimumScale = .1
	l.MaximumScale = 2
	c, e := o.request()
	if e != nil {
		t.Fatal(e)
	}
	if got := goLimits(c.limits); !reflect.DeepEqual(got, *l) {
		t.Fatalf("limits %+v != %+v", got, *l)
	}
	if float64(c.scale) != o.Scale || bool(c.include_background) || bool(c.recognition) || !bool(c.strict_images) || !bool(c.has_background) {
		t.Fatal("options lost")
	}
	if got := (Color{float64(c.background.red), float64(c.background.green), float64(c.background.blue), float64(c.background.alpha)}); got != *o.Background {
		t.Fatal(got)
	}
	if uint32(c.abi_version) != 2 || c.struct_size == 0 {
		t.Fatal("ABI not initialized")
	}
}

func TestPartialLimits(t *testing.T) {
	t.Parallel()
	o := &Options{Limits: Limits{MaximumPixels: 123}}
	c, err := o.request()
	if err != nil {
		t.Fatal(err)
	}
	want := DefaultLimits()
	want.MaximumPixels = 123
	if got := goLimits(c.limits); got != want {
		t.Fatalf("got %+v, want %+v", got, want)
	}
}

func TestInvalidLimitFields(t *testing.T) {
	t.Parallel()
	for _, field := range []string{"MaximumPixels", "MaximumDecodedPixels", "MaximumPages", "MaximumWork", "MaximumOutputBytes", "MinimumScale", "MaximumScale"} {
		t.Run(field, func(t *testing.T) {
			o := &Options{}
			v := reflect.ValueOf(&o.Limits).Elem().FieldByName(field)
			if v.Kind() == reflect.Float64 {
				v.SetFloat(-1)
			} else {
				v.SetInt(-1)
			}
			_, err := o.request()
			if err == nil || !strings.Contains(err.Error(), field) {
				t.Fatalf("%s: %v", field, err)
			}
		})
	}
}
