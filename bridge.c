// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.
#include "bridge.h"
#include "snr_internal.h"
#include "_cgo_export.h"
#include <snnote/log.h>
#include <stdlib.h>
#include <string.h>
static bool cancel(void* ctx) { return snrgoCancel((uintptr_t)ctx); }
static void progress(void* ctx, size_t completed, size_t total) { snrgoProgress((uintptr_t)ctx, completed, total); }
// Batch before crossing cgo: SVG emits base64 in four-byte pieces.
// Each operation owns this bounded C buffer until all callbacks have returned.
typedef struct {
 uintptr_t handle;
 size_t used;
 snr_status status;
 uint8_t bytes[64 * 1024];
} output_buffer;
static snr_status flush_output(output_buffer* b) {
 if (b->status == SNR_OK && b->used) {
  b->status = snrgoWrite(b->handle, b->bytes, b->used);
  b->used = 0;
 }
 return b->status;
}
static snr_status write_output(void* ctx, const uint8_t* p, size_t n) {
 output_buffer* b = ctx;
 while (n && b->status == SNR_OK) {
  size_t count = sizeof b->bytes - b->used;
  if (count > n) count = n;
  memcpy(b->bytes + b->used, p, count);
  b->used += count; p += count; n -= count;
  if (b->used == sizeof b->bytes) flush_output(b);
 }
 return b->status;
}
static snr_status finish_output(output_buffer* b, snr_status status) {
 // Do not deliver buffered bytes after a renderer failure or cancellation.
 if (b && status == SNR_OK) status = flush_output(b);
 free(b);
 return status;
}
snr_status snrgo_render(const sn_notebook* n, size_t page, size_t count, int kind,
 const snr_options* options, uintptr_t handle, bool streaming, snr_buffer* buffer,
 snr_bitmap* bitmap, snr_raster* raster, snr_error* error) {
 snr_options o = *options;
 o.cancel = cancel; o.cancel_context = (void*)handle;
 o.progress = progress; o.progress_context = (void*)handle;
 if (kind == 0) return snr_page_bitmap(n, page, &o, bitmap, error);
 if (kind == 1) return snr_page_raster(n, page, &o, raster, error);
 // Allocate only for streaming; keep the 64 KiB batch off the C stack.
 output_buffer* batch = streaming ? malloc(sizeof *batch) : nullptr;
 if (streaming && !batch)
  return snr_fail(error, SNR_ERR_OUT_OF_MEMORY, 0, "allocating output batch");
 if (batch) {
  batch->handle = handle;
  batch->used = 0;
  batch->status = SNR_OK;
 }
 snr_output out = {SNR_OUTPUT_ABI, sizeof out, write_output, batch};
 snr_status status;
 switch(kind) {
 case 2: status = streaming ? snr_page_png_to_output(n,page,&o,&out,error) : snr_page_png(n,page,&o,buffer,error); break;
 case 3: status = streaming ? snr_page_svg_to_output(n,page,&o,&out,error) : snr_page_svg(n,page,&o,buffer,error); break;
 case 4: status = streaming ? snr_page_pdf_to_output(n,page,&o,&out,error) : snr_page_pdf(n,page,&o,buffer,error); break;
 default: status = streaming ? snr_document_pdf_to_output(n,page,count,&o,&out,error) : snr_document_pdf(n,page,count,&o,buffer,error); break;
 }
 return finish_output(batch, status);
}
static void render_log(snr_log_level level, const char* msg, void* ctx) { (void)ctx; snrgoLog((int)level, (char*)msg); }
static void reader_log(sn_log_level level, const char* msg, void* ctx) { (void)ctx; snrgoLog((int)level, (char*)msg); }
static void drawing_log(spd_log_level level, const char* msg, void* ctx) { (void)ctx; snrgoLog((int)level, (char*)msg); }
void snrgo_log_install(void) { snr_log_install(render_log, nullptr); sn_log_install(reader_log, nullptr); spd_log_install(drawing_log, nullptr); }
sn_size snrgo_geometry(const sn_notebook* n, size_t page, double* dpi) {
 *dpi = snr_page_dpi(n, &n->pages[page]);
 return n->pages[page].size;
}

snr_status snrgo_overlay(const sn_notebook* n, const uint8_t* pdf, size_t size,
 size_t page, size_t count, int kind, const snr_overlay_options* options,
 uintptr_t handle, bool streaming, snr_buffer* buffer, snr_bitmap* bitmap,
 snr_overlay_result* result, snr_error* error) {
 snr_overlay_options o = *options;
 o.render.cancel = cancel; o.render.cancel_context = (void*)handle;
 o.render.progress = progress; o.render.progress_context = (void*)handle;
 // Keep the pointer-bearing options on the C stack. Go supplies only a
 // pointer-free result, so no Go memory contains a retained Go pointer.
 *result = (snr_overlay_result){.abi_version = SNR_OVERLAY_RESULT_ABI, .struct_size = sizeof *result};
 o.result = result;
 if (kind == 0) return snr_mark_page_bitmap(n,pdf,size,page,&o,bitmap,error);
 // Allocate only for streaming; keep the 64 KiB batch off the C stack.
 output_buffer* batch = streaming ? malloc(sizeof *batch) : nullptr;
 if (streaming && !batch)
  return snr_fail(error, SNR_ERR_OUT_OF_MEMORY, 0, "allocating output batch");
 if (batch) {
  batch->handle = handle;
  batch->used = 0;
  batch->status = SNR_OK;
 }
 snr_output out = {SNR_OUTPUT_ABI, sizeof out, write_output, batch};
 snr_status status;
 switch(kind) {
 case 1: status = streaming ? snr_mark_page_png_to_output(n,pdf,size,page,&o,&out,error) : snr_mark_page_png(n,pdf,size,page,&o,buffer,error); break;
 default: status = streaming ? snr_mark_document_pdf_to_output(n,pdf,size,page,count,&o,&out,error) : snr_mark_document_pdf(n,pdf,size,page,count,&o,buffer,error); break;
 }
 return finish_output(batch, status);
}

// These Go callbacks only operate on a concrete sync.Mutex, never user code.
// Upstream releases its installation mutex before invoking the host lock.
static void pdfium_lock(void* ctx) { snrgoPDFiumLock((uintptr_t)ctx); }
static void pdfium_unlock(void* ctx) { snrgoPDFiumUnlock((uintptr_t)ctx); }
snr_status snrgo_pdfium_set_lock(uintptr_t handle, snr_error* error) {
 return snr_pdfium_set_lock(pdfium_lock, pdfium_unlock, (void*)handle, error);
}

snr_status snrgo_page_text(const uint8_t* pdf, size_t size, size_t page,
 const sn_rect* rects, size_t count, const snr_pdf_text_options* options,
 uintptr_t handle, snr_buffer* buffer, snr_error* error) {
 snr_pdf_text_options o = *options;
 o.cancel = cancel; o.cancel_context = (void*)handle;
 return snr_pdf_page_text(pdf, size, page, rects, count, &o, buffer, error);
}

snr_status snrgo_drawing(const spd_drawing* drawing, int kind,
 const snr_spd_options* options, uintptr_t handle, bool streaming,
 snr_buffer* buffer, snr_bitmap* bitmap, snr_error* error) {
 snr_spd_options o = *options;
 o.render.cancel = cancel; o.render.cancel_context = (void*)handle;
 if (kind == 0) return snr_spd_bitmap(drawing,&o,bitmap,error);
 // Allocate only for streaming; keep the 64 KiB batch off the C stack.
 output_buffer* batch = streaming ? malloc(sizeof *batch) : nullptr;
 if (streaming && !batch)
  return snr_fail(error, SNR_ERR_OUT_OF_MEMORY, 0, "allocating output batch");
 if (batch) {
  batch->handle = handle;
  batch->used = 0;
  batch->status = SNR_OK;
 }
 snr_output out = {SNR_OUTPUT_ABI, sizeof out, write_output, batch};
 snr_status status;
 switch(kind) {
 case 1: status = streaming ? snr_spd_png_to_output(drawing,&o,&out,error) : snr_spd_png(drawing,&o,buffer,error); break;
 case 2: status = streaming ? snr_spd_pdf_to_output(drawing,&o,&out,error) : snr_spd_pdf(drawing,&o,buffer,error); break;
 default: status = streaming ? snr_spd_svg_to_output(drawing,&o,&out,error) : snr_spd_svg(drawing,&o,buffer,error); break;
 }
 return finish_output(batch, status);
}
