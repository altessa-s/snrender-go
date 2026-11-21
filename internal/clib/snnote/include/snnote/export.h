/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* What the shared library lets out.
 *
 * Everything inside is compiled with hidden visibility, so the plumbing —
 * the arena and the byte reader — does not reach a consumer's
 * symbol table and cannot be linked against by accident. What is marked
 * `SN_API` below is the contract, and only that. */

#ifndef SNNOTE_EXPORT_H
#define SNNOTE_EXPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(SNNOTE_BUILDING_SHARED)
#define SN_API __declspec(dllexport)
#elif defined(SNNOTE_USING_SHARED)
#define SN_API __declspec(dllimport)
#else
#define SN_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define SN_API __attribute__((visibility("default")))
#else
#define SN_API
#endif

#ifdef __cplusplus
extern "C" {}
#endif

#ifdef __cplusplus
}
#endif
#endif /* SNNOTE_EXPORT_H */
