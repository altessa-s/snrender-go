// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"debug/elf"
	"debug/macho"
	"debug/pe"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// definedSymbols answers the names of the symbols an independently built unstripped executable
// defines, without the leading underscore of Mach-O, read from its Mach-O,
// ELF or PE (COFF) symbol table.
func definedSymbols(t *testing.T) []string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "symbols.test")
	cmd := exec.Command("go", "test", "-c", "-tags="+testTags, "-ldflags=-s=false -w=false", "-o", path, ".")
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build symbol binary: %v\n%s", err, out)
	}
	var names []string
	switch runtime.GOOS {
	case "darwin":
		f, err := macho.Open(path)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		if f.Symtab == nil {
			t.Fatal("the executable has no symbol table")
		}
		for _, s := range f.Symtab.Syms {
			// N_SECT: defined in a section of this file. The package is
			// compiled with hidden visibility, so the linker demotes every
			// symbol outside the public API from external (N_EXT) to
			// private external (N_PEXT); those collide at link time all
			// the same and are the larger part of what must be renamed.
			// A static symbol carries neither bit.
			if s.Type&0x0e == 0x0e && s.Type&0x11 != 0 && s.Sect != 0 {
				names = append(names, strings.TrimPrefix(s.Name, "_"))
			}
		}
	case "linux":
		f, err := elf.Open(path)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		syms, err := f.Symbols()
		if err != nil {
			t.Fatal("the executable has no symbol table")
		}
		for _, s := range syms {
			// A hidden symbol is local in the linked executable but keeps
			// its hidden visibility, which tells it from a static one.
			bind := elf.ST_BIND(s.Info)
			hidden := elf.ST_VISIBILITY(s.Other) == elf.STV_HIDDEN
			if s.Section != elf.SHN_UNDEF && (bind == elf.STB_GLOBAL || bind == elf.STB_WEAK || hidden) {
				names = append(names, s.Name)
			}
		}
	case "windows":
		f, err := pe.Open(path)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		if len(f.Symbols) == 0 {
			t.Fatal("the executable has no symbol table")
		}
		for _, s := range f.Symbols {
			// External (storage class 2) and defined in a section of
			// this file; x86-64 COFF names carry no leading underscore.
			if s.StorageClass == 2 && s.SectionNumber > 0 {
				names = append(names, s.Name)
			}
		}
	default:
		t.Skip("no symbol reader for " + runtime.GOOS)
	}
	return names
}

// The vendored libraries are linked under snrgo_ names only, so that the
// C libraries snnote-go compile under their own
// names can share a program with this package. A symbol scripts/vendor.py
// did not rename would show here under its own name.
func TestVendoredSymbolsArePrefixed(t *testing.T) {
	renamed := 0
	for _, name := range definedSymbols(t) {
		if strings.HasPrefix(name, "snrgo_sn") || strings.HasPrefix(name, "snrgo_spe_") || strings.HasPrefix(name, "snrgo_spd_") {
			renamed++
			continue
		}
		for _, prefix := range []string{"sn_", "snr_", "spe_", "spd_", "sqlite3"} {
			if strings.HasPrefix(name, prefix) {
				t.Errorf("%s is linked under its own name", name)
			}
		}
	}
	if renamed < 40 {
		t.Fatalf("only %d renamed symbols found", renamed)
	}
}
