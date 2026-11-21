// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef SNRGO_BRIDGE_H
#define SNRGO_BRIDGE_H
#include <snrender/snrender.h>
#include <stdlib.h>
#include <snspd/snspd.h>
#define SNRGO_REFUSED ((snr_status)0x52454655u)
snr_status snrgo_render(const sn_notebook*, size_t, size_t, int, const snr_options*, uintptr_t, bool, snr_buffer*, snr_bitmap*, snr_raster*, snr_error*);
snr_status snrgo_overlay(const sn_notebook*, const uint8_t*, size_t, size_t, size_t, int, const snr_overlay_options*, uintptr_t, bool, snr_buffer*, snr_bitmap*, snr_overlay_result*, snr_error*);
snr_status snrgo_pdfium_set_lock(uintptr_t, snr_error*);
void snrgo_log_install(void);
sn_size snrgo_geometry(const sn_notebook*, size_t, double*);
snr_status snrgo_page_text(const uint8_t*, size_t, size_t, const sn_rect*, size_t, const snr_pdf_text_options*, uintptr_t, snr_buffer*, snr_error*);
snr_status snrgo_drawing(const spd_drawing*, int, const snr_spd_options*, uintptr_t, bool, snr_buffer*, snr_bitmap*, snr_error*);
#endif
