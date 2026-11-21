/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
// Synthetic-only failure injection. The getter must borrow self: a strong C
// parameter would itself leak when throwing without ARC exception cleanup.
#import <PDFKit/PDFKit.h>
#import <objc/runtime.h>
#include "snr_pdf_text.h"
#include "internal/clib/snrender/backends/apple/snr_pdf_text.m"
#include <stdio.h>
static IMP original_dealloc;
static unsigned destroyed;
static unsigned pools_drained;
static bool malloc_exception;
@interface PoolToken : NSObject
+ (instancetype)token;
@end
@implementation PoolToken
+ (instancetype)token { return [self new]; }
- (void)dealloc { pools_drained++; }
@end
static void throw_exception(void) {
 // An unused convenience-method result remains autoreleased in the current
 // engine pool. It must be destroyed before snr_source_text returns.
 [PoolToken token];
 @throw [NSException exceptionWithName:malloc_exception ? NSMallocException : @"SyntheticProbe"
                              reason:@"synthetic" userInfo:nil];
}
static NSUInteger throw_count(__unsafe_unretained id self, SEL cmd) {
 (void)self; (void)cmd;
 throw_exception();
 return 0;
}
static CGRect throw_bounds(__unsafe_unretained id self, SEL cmd, __unsafe_unretained PDFPage* page) {
 (void)self; (void)cmd; (void)page;
 throw_exception();
 return CGRectNull;
}
static void count_dealloc(__unsafe_unretained id self, SEL cmd) {
 destroyed++;
 ((void (*)(__unsafe_unretained id,SEL))original_dealloc)(self,cmd);
}
snr_status snr_pdf_text_charge(snr_pdf_text_state* state, size_t count) { (void)state; (void)count; return SNR_OK; }
snr_status snr_pdf_text_character(snr_pdf_text_state* state, uint32_t scalar, double a, double b, double c, double d) {
 (void)state; (void)scalar; (void)a; (void)b; (void)c; (void)d; return SNR_OK;
}
int main(void) {
 @autoreleasepool {
  NSData* pdf=[NSData dataWithContentsOfFile:@"testdata/pdf-text/r0-offset0.pdf"];
  if (!pdf) return 1;
  Class cls=[PDFDocument class];
  Method destructor=class_getInstanceMethod(cls,NSSelectorFromString(@"dealloc"));
  original_dealloc=method_getImplementation(destructor);
  if (!class_addMethod(cls,NSSelectorFromString(@"dealloc"),(IMP)count_dealloc,method_getTypeEncoding(destructor)))
   method_setImplementation(destructor,(IMP)count_dealloc);
  Method page_count=class_getInstanceMethod(cls,@selector(pageCount));
  IMP original_count=method_getImplementation(page_count);
  Method bounds=class_getInstanceMethod([PDFSelection class],@selector(boundsForPage:));
  for (unsigned phase=0;phase<4;phase++) {
   malloc_exception=(phase%2)!=0;
   method_setImplementation(page_count,phase<2?(IMP)throw_count:original_count);
   if (phase==2) method_setImplementation(bounds,(IMP)throw_bounds);
   for (unsigned i=0;i<100;i++) {
    snr_pdf_text_state state={.page_count=1,.remaining=SIZE_MAX,
                             .options={.maximum_characters=SIZE_MAX}};
    snr_status expected=malloc_exception?SNR_ERR_OUT_OF_MEMORY:SNR_ERR_BACKEND;
    if (snr_source_text(pdf.bytes,pdf.length,&state)!=expected) return 2;
    if (pools_drained!=phase*100+i+1) return 4;
   }
  }
  printf("PDFKit injected exceptions: released %u/400 documents, drained %u/400 pools\n",destroyed,pools_drained);
  return destroyed==400?0:3;
 }
}
