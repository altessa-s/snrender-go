// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender

// LiveHandles counts callback state still owned by a rendering call.
func LiveHandles() int64 { return liveHandles.Load() }

// LiveNotebooks counts C notebooks whose storage has not been freed.
func LiveNotebooks() int64 { return liveNotebooks.Load() }
