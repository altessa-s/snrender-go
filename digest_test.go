// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"crypto/sha256"
	"fmt"
	"io/fs"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"github.com/altessa-s/snrender-go"
)

// Sorting slash-separated paths matches the generator on every supported OS.
func TestVendoredDigest(t *testing.T) {
	const root = "internal/clib"
	var paths []string
	err := filepath.WalkDir(root, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !entry.IsDir() {
			relative, err := filepath.Rel(root, path)
			if err != nil {
				return err
			}
			paths = append(paths, filepath.ToSlash(relative))
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	sort.Strings(paths)
	h := sha256.New()
	for _, path := range paths {
		fmt.Fprintf(h, "%x %s\n", sha256.Sum256(read(t, filepath.Join(root, filepath.FromSlash(path)))), path)
	}
	want := fmt.Sprintf(`#define SNRGO_DIGEST "%x"`, h.Sum(nil))
	if !strings.Contains(string(read(t, "clib_digest.h")), want) {
		t.Fatalf("vendored digest mismatch: want %s", want)
	}
	var rendererVersion string
	for _, line := range strings.Split(string(read(t, filepath.Join(root, "VERSION"))), "\n") {
		fields := strings.Fields(line)
		if len(fields) == 3 && fields[0] == "snrender-lib" {
			rendererVersion = fields[1]
		}
	}
	if rendererVersion == "" || snrender.LibraryVersion != rendererVersion {
		t.Fatalf("LibraryVersion = %q, vendored VERSION = %q; run scripts/vendor.py", snrender.LibraryVersion, rendererVersion)
	}
}
