// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

/*
#include "bridge.h"
*/
import "C"
import (
	"errors"
	"fmt"
)

// Status is a renderer result code. Reader failures retain their own code in ParseError.
type Status uint32

// Renderer statuses mirror the C ABI.
const (
	StatusOK Status = iota
	StatusInvalidArgument
	StatusOutOfMemory
	StatusLimit
	StatusUnsupported
	StatusBackend
	StatusCancelled
)

// String uses the linked library's diagnostics, including its unknown-code fallback.
func (s Status) String() string { return C.GoString(C.snr_status_string(C.snr_status(s))) }

// Error retains the library diagnostic and zero-based page index.
type Error struct {
	Status  Status
	Index   uint64
	Message string
	Cause   error
}

func (e *Error) Error() string {
	if e.Message == "" {
		return fmt.Sprintf("snrender: %s", e.Status)
	}
	return fmt.Sprintf("snrender: %s: %s", e.Status, e.Message)
}

// Unwrap preserves context and output errors for errors.Is.
func (e *Error) Unwrap() error { return e.Cause }

// Is compares renderer statuses without depending on diagnostic wording.
func (e *Error) Is(target error) bool { t, ok := target.(*Error); return ok && e.Status == t.Status }

// Sentinels distinguish the renderer's failure classes.
var (
	ErrInvalidArgument = &Error{Status: StatusInvalidArgument}
	ErrOutOfMemory     = &Error{Status: StatusOutOfMemory}
	ErrLimit           = &Error{Status: StatusLimit}
	ErrUnsupported     = &Error{Status: StatusUnsupported}
	ErrBackend         = &Error{Status: StatusBackend}
	ErrCancelled       = &Error{Status: StatusCancelled}
	ErrClosed          = errors.New("snrender: notebook is closed")
)

func invalid(message string) error { return &Error{Status: StatusInvalidArgument, Message: message} }
func cError(status C.snr_status, e *C.snr_error) *Error {
	return &Error{Status: Status(status), Index: uint64(e.index), Message: C.GoString(&e.message[0])}
}

// ParseStatus is a status from the embedded reader, kept separate from Status.
type ParseStatus int

// Reader statuses retain their independent C ABI values.
const (
	ParseOK ParseStatus = iota
	ParseInvalidArgument
	ParseOutOfMemory
	ParseUnsupportedFormat
	ParseUnexpectedEOF
	ParseMalformed
	ParseImageEncoding
	ParseUnsupportedEncoding
	ParseLimit
)

// ParseError reports a reader failure and its format offset or count.
type ParseError struct {
	Status  ParseStatus
	Value   uint64
	Message string
}

// String uses the reader's diagnostic names, including unknown statuses.
func (s ParseStatus) String() string { return C.GoString(C.sn_status_string(C.sn_status(s))) }

func (e *ParseError) Error() string {
	if e.Message == "" {
		return fmt.Sprintf("snrender: parse: %s", e.Status)
	}
	return fmt.Sprintf("snrender: parse: %s: %s", e.Status, e.Message)
}

// Is matches a reader status, and common renderer failure classes where applicable.
func (e *ParseError) Is(target error) bool {
	if t, ok := target.(*ParseError); ok {
		return e.Status == t.Status
	}
	switch e.Status {
	case ParseInvalidArgument:
		return target == ErrInvalidArgument
	case ParseOutOfMemory:
		return target == ErrOutOfMemory
	case ParseLimit:
		return target == ErrLimit
	case ParseUnsupportedFormat, ParseUnsupportedEncoding, ParseImageEncoding:
		return target == ErrUnsupported
	}
	return false
}

// Reader sentinels preserve each distinct parse failure.
var (
	ErrParseInvalidArgument = &ParseError{Status: ParseInvalidArgument}
	ErrParseOutOfMemory     = &ParseError{Status: ParseOutOfMemory}
	ErrUnsupportedFormat    = &ParseError{Status: ParseUnsupportedFormat}
	ErrUnexpectedEOF        = &ParseError{Status: ParseUnexpectedEOF}
	ErrMalformed            = &ParseError{Status: ParseMalformed}
	ErrImageEncoding        = &ParseError{Status: ParseImageEncoding}
	ErrUnsupportedEncoding  = &ParseError{Status: ParseUnsupportedEncoding}
	ErrParseLimit           = &ParseError{Status: ParseLimit}
)
