// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.
#ifndef SNRENDER_GO_PRELUDE_H
#define SNRENDER_GO_PRELUDE_H
// ISO mode disables contraction in GCC; Clang also needs this pragma.
#ifdef __clang__
#pragma STDC FP_CONTRACT OFF
#endif
#if defined(SNR_TESTING) || defined(SNRENDER_BUILDING) || defined(SNRENDER_SHARED) || defined(SNNOTE_BUILDING_SHARED) || defined(SNNOTE_USING_SHARED) || defined(SNSPD_BUILDING_SHARED) || defined(SNSPD_USING_SHARED)
#error "snrender-go requires production static C libraries"
#endif
#include "clib_symbols.h"
#endif
