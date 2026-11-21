// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

/*
#include "bridge.h"
*/
import "C"

// DrawingPage selects the framing of an Atelier drawing.
type DrawingPage uint32

const (
	// DrawingPaintedBounds moves the selected stored tile union to the top-left.
	// Transparent and damaged tiles count; an empty union produces a 1x1 page.
	DrawingPaintedBounds DrawingPage = iota
	// DrawingCanvas clips to canvas dimensions centred on the saved viewport.
	// This anchoring is inferred from one export, not established format semantics.
	DrawingCanvas
)

// DrawingParseLimits bounds the private SQLite reader, independently of rendering.
// Zero fields select library defaults. Work meters allocation/copy, traversal and
// SQLite VM instructions; parsing is synchronous and cannot be cancelled midway.
type DrawingParseLimits struct {
	MaximumFileSize, MaximumLayers, MaximumTilesPerLayer, MaximumTileBytes              int
	MaximumTotalTileBytes, MaximumConfigEntries, MaximumConfigBytes, MaximumMemoryBytes int
	MaximumWork                                                                         uint64
}

// DrawingParseOptions controls parsing within this module; no snspd-go objects
// cross the C boundary. Strict rejects unknown format versions. RequirePNG128
// checks only PNG signatures/IHDR; StrictTiles controls full render validation.
type DrawingParseOptions struct {
	Limits                DrawingParseLimits
	Strict, RequirePNG128 bool
}

// DefaultDrawingParseOptions reads the embedded reader's independent defaults.
func DefaultDrawingParseOptions() DrawingParseOptions {
	c := C.spd_options_default()
	l := c.limits
	return DrawingParseOptions{Limits: DrawingParseLimits{
		MaximumFileSize: int(l.max_file_size), MaximumLayers: int(l.max_layers), MaximumTilesPerLayer: int(l.max_tiles_per_layer), MaximumTileBytes: int(l.max_tile_bytes), MaximumTotalTileBytes: int(l.max_total_tile_bytes), MaximumConfigEntries: int(l.max_config_entries), MaximumConfigBytes: int(l.max_config_bytes), MaximumMemoryBytes: int(l.max_memory_bytes), MaximumWork: uint64(l.max_work),
	}, Strict: bool(c.strict), RequirePNG128: bool(c.require_png_128)}
}

func (o DrawingParseOptions) request() (C.spd_options, error) {
	c := C.spd_options_default()
	c.strict, c.require_png_128 = C.bool(o.Strict), C.bool(o.RequirePNG128)
	l := o.Limits
	for _, f := range []struct {
		value  int
		target *C.size_t
		name   string
	}{
		{l.MaximumFileSize, &c.limits.max_file_size, "MaximumFileSize"},
		{l.MaximumLayers, &c.limits.max_layers, "MaximumLayers"},
		{l.MaximumTilesPerLayer, &c.limits.max_tiles_per_layer, "MaximumTilesPerLayer"},
		{l.MaximumTileBytes, &c.limits.max_tile_bytes, "MaximumTileBytes"},
		{l.MaximumTotalTileBytes, &c.limits.max_total_tile_bytes, "MaximumTotalTileBytes"},
		{l.MaximumConfigEntries, &c.limits.max_config_entries, "MaximumConfigEntries"},
		{l.MaximumConfigBytes, &c.limits.max_config_bytes, "MaximumConfigBytes"},
		{l.MaximumMemoryBytes, &c.limits.max_memory_bytes, "MaximumMemoryBytes"},
	} {
		v, err := sizeLimit(f.value, *f.target, "Parse.Limits."+f.name)
		if err != nil {
			return c, err
		}
		*f.target = v
	}
	if l.MaximumWork != 0 {
		c.limits.max_work = C.uint64_t(l.MaximumWork)
	}
	return c, nil
}

// DrawingOptions controls one parse and render. Nil and zero fields select
// library defaults: painted bounds, scale 1, white, including the reference layer.
// Nonzero DPI overrides Scale with DPI/canvas PPI (missing PPI defaults to 300).
// PDF physical size is independent of Scale/DPI. Background is straight sRGB.
// Keep options and input immutable until the call returns.
type DrawingOptions struct {
	Page                              DrawingPage
	Scale, DPI                        float64
	Background                        *Color
	HideReferenceLayer, StrictTiles   bool
	Limits                            Limits
	MaximumTiles, MaximumDecodedBytes int
	Parse                             DrawingParseOptions
}

// DefaultDrawingOptions reads independent renderer and reader defaults.
func DefaultDrawingOptions() DrawingOptions {
	c := C.snr_spd_options_default()
	r := goOptions(c.render)
	page := DrawingPaintedBounds
	if c.page == C.SNR_SPD_CANVAS {
		page = DrawingCanvas
	}
	return DrawingOptions{Page: page, Scale: r.Scale, DPI: float64(c.dpi), Background: r.Background, HideReferenceLayer: !bool(c.include_reference_layer), StrictTiles: r.StrictImages, Limits: r.Limits, MaximumTiles: int(c.maximum_tiles), MaximumDecodedBytes: int(c.maximum_decoded_bytes), Parse: DefaultDrawingParseOptions()}
}

func (o *DrawingOptions) request() (C.snr_spd_options, C.spd_options, error) {
	c := C.snr_spd_options_default()
	var parse DrawingParseOptions
	if o == nil {
		p, e := parse.request()
		return c, p, e
	}
	p, err := o.Parse.request()
	if err != nil {
		return c, p, err
	}
	r, err := (&Options{Scale: o.Scale, Background: o.Background, StrictImages: o.StrictTiles, Limits: o.Limits}).request()
	if err != nil {
		return c, p, err
	}
	c.render = r
	switch o.Page {
	case DrawingPaintedBounds:
		c.page = C.SNR_SPD_PAINTED_BOUNDS
	case DrawingCanvas:
		c.page = C.SNR_SPD_CANVAS
	default:
		return c, p, invalid("unknown drawing page mode")
	}
	c.dpi = C.double(o.DPI)
	c.include_reference_layer = C.bool(!o.HideReferenceLayer)
	c.maximum_tiles, err = sizeLimit(o.MaximumTiles, c.maximum_tiles, "MaximumTiles")
	if err != nil {
		return c, p, err
	}
	c.maximum_decoded_bytes, err = sizeLimit(o.MaximumDecodedBytes, c.maximum_decoded_bytes, "MaximumDecodedBytes")
	return c, p, err
}
