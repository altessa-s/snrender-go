/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef SNNOTE_H
#define SNNOTE_H
#include "snnote/log.h"
#include "snnote/parser.h"
#include "snnote/raster.h"
#include "snnote/types.h"

#ifdef __cplusplus
extern "C" {
#endif
#define SNNOTE_VERSION_MAJOR 1
#define SNNOTE_VERSION_MINOR 0
#define SNNOTE_VERSION_PATCH 0
#define SNNOTE_STRINGIFY_(x) #x
#define SNNOTE_STRINGIFY(x) SNNOTE_STRINGIFY_(x)
#define SNNOTE_VERSION_STRING                                      \
  SNNOTE_STRINGIFY(SNNOTE_VERSION_MAJOR)                           \
  "." SNNOTE_STRINGIFY(SNNOTE_VERSION_MINOR) "." SNNOTE_STRINGIFY( \
      SNNOTE_VERSION_PATCH)
#ifdef __cplusplus
extern "C" {}
#endif

#ifdef __cplusplus
}
#endif
#endif
