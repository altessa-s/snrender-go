/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "sn_internal.h"

/* The sink and its context move as one: read separately, a line could reach a
 * new sink with the old sink's context. The pair is published behind a single
 * atomic pointer, and a replacement uses a retained pair rather than editing
 * the live one. */
typedef struct sn_log_binding {
  sn_log_sink sink;
  void* context;
} sn_log_binding;

static _Atomic(sn_log_binding*) sn_log_installed = nullptr;

/* Retain every installed binding so callbacks that already observed one can
 * finish safely. Reinstalling a retained pair needs no new storage. */
typedef struct sn_log_retired {
  struct sn_log_retired* next;
  sn_log_binding binding;
} sn_log_retired;

static _Atomic(sn_log_retired*) sn_log_history = nullptr;

#ifdef SNNOTE_TESTING
static _Thread_local size_t binding_allocations;
size_t sn_test_log_allocations(void) { return binding_allocations; }
#endif

/* Publish callback and context as one immutable binding; allocation failure
 * retains the current sink. */
void sn_log_install(sn_log_sink sink, void* context) {
  if (sink == nullptr) {
    sn_log_silence();
    return;
  }
  /* Acquire the immutable, never-removed entries published by other installers.
   * Concurrent installs of the same new pair may both allocate; that is safe.
   */
  for (sn_log_retired* entry =
           atomic_load_explicit(&sn_log_history, memory_order_acquire);
       entry != nullptr; entry = entry->next) {
    if (entry->binding.sink == sink && entry->binding.context == context) {
      atomic_store_explicit(&sn_log_installed, &entry->binding,
                            memory_order_release);
      return;
    }
  }
#ifdef SNNOTE_TESTING
  binding_allocations++;
#endif
  sn_log_retired* entry = malloc(sizeof *entry);
  if (entry == nullptr) return;
  entry->binding = (sn_log_binding){.sink = sink, .context = context};
  sn_log_retired* head =
      atomic_load_explicit(&sn_log_history, memory_order_relaxed);
  do {
    entry->next = head;
  } while (!atomic_compare_exchange_weak_explicit(&sn_log_history, &head, entry,
                                                  memory_order_release,
                                                  memory_order_relaxed));
  atomic_store_explicit(&sn_log_installed, &entry->binding,
                        memory_order_release);
}

/* Publish silence without invalidating bindings already read by parsing
 * threads. */
void sn_log_silence(void) {
  atomic_store_explicit(&sn_log_installed, nullptr, memory_order_release);
}

/* Observe whether a sink is installed without retaining or invoking it. */
bool sn_log_wanted(void) {
  return atomic_load_explicit(&sn_log_installed, memory_order_acquire) !=
         nullptr;
}

/* Host callbacks are outside the parser's work and allocation scope. A
 * callback may parse another file or invoke standalone bitmap helpers. */
static void deliver(sn_log_binding* binding, sn_log_level level,
                    const char* message) {
  sn_work_meter* saved = sn_meter;
  sn_meter = nullptr;
  binding->sink(level, message, binding->context);
  sn_meter = saved;
}

/* Format a content-free diagnostic against one atomic binding snapshot, keeping
 * callback and context paired. */
void sn_log_writef(sn_log_level level, const char* format, ...) {
  sn_log_binding* binding =
      atomic_load_explicit(&sn_log_installed, memory_order_acquire);
  if (binding == nullptr) return;

  /* One line, one stack buffer. The messages are counts, block types and
   * raw style numbers — never a page's text — so a line that does not fit
   * is rare; it goes to the heap rather than out cut short, so that every
   * line is handed over whole. */
  char message[512];
  va_list args;
  va_start(args, format);
  va_list again;
  va_copy(again, args);
  int written = vsnprintf(message, sizeof message, format, args);
  va_end(args);
  if (written < 0) {
    va_end(again);
    return;
  }
  if ((size_t)written < sizeof message) {
    va_end(again);
    deliver(binding, level, message);
    return;
  }
  char* whole = malloc((size_t)written + 1u);
  if (whole == nullptr) {
    va_end(again);
    /* Out of memory: the line cut short beats no line at all. */
    deliver(binding, level, message);
    return;
  }
  (void)vsnprintf(whole, (size_t)written + 1u, format, again);
  va_end(again);
  deliver(binding, level, whole);
  free(whole);
}
