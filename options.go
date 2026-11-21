// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

/*
#include "bridge.h"
*/
import "C"
import (
	"math"
	"strings"
)

// Color is straight sRGB and coverage, with finite channels in [0,1].
type Color struct{ Red, Green, Blue, Alpha float64 }

// Limits bounds one rendering call. Zero fields select library defaults.
// Counts and per-page work use int, matching size_t without narrowing on supported
// platforms. Cumulative document work uses uint64, matching the C uint64_t.
type Limits struct {
	MaximumPixels, MaximumDecodedPixels, MaximumPages, MaximumWork, MaximumOutputBytes int
	MaximumDocumentWork                                                                uint64
	MinimumScale, MaximumScale                                                         float64
}

// DefaultLimits reads the defaults from the linked C library.
func DefaultLimits() Limits { return goLimits(C.snr_limits_default()) }
func goLimits(l C.snr_limits) Limits {
	return Limits{
		MaximumPixels: int(l.maximum_pixels), MaximumDecodedPixels: int(l.maximum_decoded_pixels), MaximumPages: int(l.maximum_pages), MaximumWork: int(l.maximum_work), MaximumOutputBytes: int(l.maximum_output_bytes), MaximumDocumentWork: uint64(l.maximum_document_work), MinimumScale: float64(l.minimum_scale), MaximumScale: float64(l.maximum_scale),
	}
}

// Options controls composition. Nil and the zero value select C defaults.
// Limits and Background are copied for the call; callers must not mutate them during it.
type Options struct {
	Scale              float64 // Zero selects the library default.
	HideBackground     bool    // Hides the stored template, not the canvas color.
	DisableRecognition bool    // Disables existing recognition; no OCR is performed.
	// StrictImages aborts on damaged image layers instead of skipping them.
	StrictImages bool
	Background   *Color // Nil selects white for notebooks, transparent for marks.
	Limits       Limits
	Title        string                     // Empty selects the library's PDF title.
	Progress     func(completed, total int) // Runs synchronously on the callback worker.
}

// DefaultOptions reads defaults without sharing mutable limit storage.
func DefaultOptions() Options {
	return goOptions(C.snr_options_default())
}

func goOptions(c C.snr_options) Options {
	l := goLimits(c.limits)
	o := Options{Scale: float64(c.scale), HideBackground: !bool(c.include_background), DisableRecognition: !bool(c.recognition), StrictImages: bool(c.strict_images), Limits: l}
	if c.has_background {
		o.Background = &Color{float64(c.background.red), float64(c.background.green), float64(c.background.blue), float64(c.background.alpha)}
	}
	return o
}
func (o *Options) request() (C.snr_options, error) {
	c := C.snr_options_default()
	if o == nil {
		return c, nil
	}
	if strings.ContainsRune(o.Title, 0) {
		return c, invalid("title contains NUL")
	}
	if o.Scale != 0 {
		c.scale = C.double(o.Scale)
	}
	if o.HideBackground {
		c.include_background = false
	}
	if o.DisableRecognition {
		c.recognition = false
	}
	c.strict_images = C.bool(o.StrictImages)
	if b := o.Background; b != nil {
		c.has_background = true
		c.background = C.snr_color{red: C.double(b.Red), green: C.double(b.Green), blue: C.double(b.Blue), alpha: C.double(b.Alpha)}
	}
	l := o.Limits
	if l.MaximumPixels < 0 {
		return c, invalid("Limits.MaximumPixels must not be negative")
	}
	if uint64(l.MaximumPixels) > uint64(^C.size_t(0)) {
		return c, invalid("Limits.MaximumPixels does not fit size_t")
	}
	if l.MaximumPixels != 0 {
		c.limits.maximum_pixels = C.size_t(l.MaximumPixels)
	}
	if l.MaximumDecodedPixels < 0 {
		return c, invalid("Limits.MaximumDecodedPixels must not be negative")
	}
	if uint64(l.MaximumDecodedPixels) > uint64(^C.size_t(0)) {
		return c, invalid("Limits.MaximumDecodedPixels does not fit size_t")
	}
	if l.MaximumDecodedPixels != 0 {
		c.limits.maximum_decoded_pixels = C.size_t(l.MaximumDecodedPixels)
	}
	if l.MaximumPages < 0 {
		return c, invalid("Limits.MaximumPages must not be negative")
	}
	if uint64(l.MaximumPages) > uint64(^C.size_t(0)) {
		return c, invalid("Limits.MaximumPages does not fit size_t")
	}
	if l.MaximumPages != 0 {
		c.limits.maximum_pages = C.size_t(l.MaximumPages)
	}
	if l.MaximumWork < 0 {
		return c, invalid("Limits.MaximumWork must not be negative")
	}
	if uint64(l.MaximumWork) > uint64(^C.size_t(0)) {
		return c, invalid("Limits.MaximumWork does not fit size_t")
	}
	if l.MaximumWork != 0 {
		c.limits.maximum_work = C.size_t(l.MaximumWork)
	}
	if l.MaximumOutputBytes < 0 {
		return c, invalid("Limits.MaximumOutputBytes must not be negative")
	}
	if uint64(l.MaximumOutputBytes) > uint64(^C.size_t(0)) {
		return c, invalid("Limits.MaximumOutputBytes does not fit size_t")
	}
	if l.MaximumOutputBytes != 0 {
		c.limits.maximum_output_bytes = C.size_t(l.MaximumOutputBytes)
	}
	if l.MaximumDocumentWork != 0 {
		c.limits.maximum_document_work = C.uint64_t(l.MaximumDocumentWork)
	}
	if math.IsNaN(l.MinimumScale) || math.IsInf(l.MinimumScale, 0) || l.MinimumScale < 0 {
		return c, invalid("Limits.MinimumScale must be finite and nonnegative")
	}
	if l.MinimumScale != 0 {
		c.limits.minimum_scale = C.double(l.MinimumScale)
	}
	if math.IsNaN(l.MaximumScale) || math.IsInf(l.MaximumScale, 0) || l.MaximumScale < 0 {
		return c, invalid("Limits.MaximumScale must be finite and nonnegative")
	}
	if l.MaximumScale != 0 {
		c.limits.maximum_scale = C.double(l.MaximumScale)
	}
	return c, nil
}

func sizeLimit(value int, fallback C.size_t, name string) (C.size_t, error) {
	if value < 0 {
		return 0, invalid(name + " must not be negative")
	}
	if uint64(value) > uint64(^C.size_t(0)) {
		return 0, invalid(name + " does not fit size_t")
	}
	if value == 0 {
		return fallback, nil
	}
	return C.size_t(value), nil
}
