//go:build darwin && !snrender_cairo

// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"bytes"
	"os/exec"
	"path/filepath"
	"testing"
)

// Exercise upstream's explicit cleanup under plain -fobjc-arc by injecting
// PDFKit exceptions and observing deallocation.
func TestPDFKitExceptionCleanup(t *testing.T) {
	path := filepath.Join(t.TempDir(), "pdfkit-exceptions")
	cmd := exec.Command("cc", "-std=c2x", "-fobjc-arc", "-I.", "-Iinternal/clib/snrender/include", "-Iinternal/clib/snrender/src", "-Iinternal/clib/snnote/include", "-framework", "PDFKit", "-framework", "CoreGraphics", "-framework", "Foundation", "testdata/pdf-text/exception_cleanup.m", "-o", path)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build PDFKit exception probe: %v\n%s", err, out)
	}
	out, err := exec.Command(path).CombinedOutput()
	if err != nil || !bytes.Contains(out, []byte("released 400/400 documents, drained 400/400 pools")) {
		t.Fatalf("PDFKit exception cleanup: %v\n%s", err, out)
	}
}
