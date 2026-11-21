// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

/*
#include "bridge.h"
*/
import "C"

// DocumentKind identifies the source layout, which the mark reader cannot infer.
type DocumentKind uint32

// Source document kinds. EPUB overlay layout is unsupported.
const (
	DocumentPDF DocumentKind = iota
	DocumentEPUB
)

// OverlayLimits bounds source parsing. Zero fields select library defaults.
type OverlayLimits struct {
	MaximumSourceBytes int // Default 512 MiB; scratch uses max(1 MiB, 16 * source bytes).
}

// OverlayResult records diagnostics, including partial work on failure.
// NoPairsAgree only describes checked pairs; marks without pairs offer no
// evidence that they belong to the supplied PDF.
type OverlayResult struct {
	AnnotatedPagesDrawn, SheetsDropped, HighlightsDropped int
	WorstPairedResidual                                   float64 // Sheet pixels.
	PagesCheckedByPairs, SignatureFields                  int
	AllAnnotationsDropped, NoPairsAgree                   bool
}

// OverlayOptions controls annotations drawn over a source PDF. Nil and the
// zero value select C defaults. Render controls scale, limits and callbacks.
// Strict rejects annotations outside the source and paired geometry differing
// by more than three sheet pixels. It cannot prove document identity.
// Result is optional, replaced on return from C even on failure. Do not share
// it between concurrent calls. Other options are borrowed and must not change
// during the call. SourceKind defaults to DocumentPDF.
type OverlayOptions struct {
	Render     Options
	SourceKind DocumentKind
	Strict     bool
	Limits     OverlayLimits
	Result     *OverlayResult
}

// DefaultOverlayOptions reads independent defaults from the linked C library.
func DefaultOverlayOptions() OverlayOptions {
	c := C.snr_overlay_options_default()
	return OverlayOptions{Render: goOptions(c.render), SourceKind: DocumentKind(c.source_kind), Strict: bool(c.strict), Limits: OverlayLimits{MaximumSourceBytes: int(c.limits.maximum_source_bytes)}}
}

func (o *OverlayOptions) request() (C.snr_overlay_options, error) {
	c := C.snr_overlay_options_default()
	if o == nil {
		return c, nil
	}
	render, err := o.Render.request()
	if err != nil {
		return c, err
	}
	c.render = render
	c.source_kind = C.snr_document_kind(o.SourceKind)
	c.strict = C.bool(o.Strict)
	limit := o.Limits.MaximumSourceBytes
	if limit < 0 {
		return c, invalid("Limits.MaximumSourceBytes must not be negative")
	}
	if uint64(limit) > uint64(^C.size_t(0)) {
		return c, invalid("Limits.MaximumSourceBytes does not fit size_t")
	}
	if limit != 0 {
		c.limits.maximum_source_bytes = C.size_t(limit)
	}
	return c, nil
}

func goOverlayResult(r C.snr_overlay_result) OverlayResult {
	return OverlayResult{
		AnnotatedPagesDrawn: int(r.annotated_pages_drawn), SheetsDropped: int(r.sheets_dropped), HighlightsDropped: int(r.highlights_dropped),
		WorstPairedResidual: float64(r.worst_paired_residual), PagesCheckedByPairs: int(r.pages_checked_by_pairs), SignatureFields: int(r.signature_fields),
		AllAnnotationsDropped: bool(r.all_annotations_dropped), NoPairsAgree: bool(r.no_pairs_agree),
	}
}
