// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

// Package snrender renders Supernote notebooks, marks and Atelier drawings
// through vendored C23 libraries, and extracts text from source PDF rectangles.
// Open owns a C copy; callers release it with Close. Immutable notebooks support
// concurrent calls. Close prevents new calls and returns immediately; active
// calls retain C storage until they finish. Returned images and bytes belong to Go.
// Writer, context Err and progress callbacks run on a synchronous helper
// goroutine; a panic or Goexit propagates after C cleanup. Callbacks may close
// the notebook or start independent calls, but must not wait for their own call.
// Log callbacks contain abnormal completion and lose only that diagnostic.
// Cancellation returns an *Error matching both ErrCancelled and the context's
// error. A ParseError also matches the corresponding general resource, argument
// or unsupported sentinel in addition to its specific parse sentinel.
// Overlay methods draw a mark over source PDF pages; indexes address source
// pages, not stored mark sheets. Cairo builds require PDFium. A process sharing
// PDFium with other libraries must coordinate all users through SetPDFiumLock.
// PageText selects character centres in a union of PDF-point Rects relative to
// the displayed rotated CropBox. It performs no OCR; PDFKit/PDFium reading order,
// ligatures and soft hyphens may differ. Its cancellation and PDFium locking
// follow previews. Empty unions succeed with an empty string.
// DrawingBitmap, DrawingPNG, DrawingPDF and DrawingSVG parse .spd bytes within
// this module. Writer variants share the notebook callback contracts. Default
// framing is painted bounds, with a white background and the reference layer.
// DrawingCanvas centres on the saved viewport (an inference from one export).
// Scale/DPI changes resolution, not PDF physical size. Reader limits bound the
// synchronous parse, which cannot be interrupted midway; rendering is cancellable.
// DrawingParseError preserves signed SQLite diagnostics and matches the existing
// parse/resource sentinels. All drawing C symbols and its private static SQLite
// are isolated from snspd-go, other bindings and host SQLite.
package snrender
