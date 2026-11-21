/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <stdalign.h>

#include "sn_internal.h"
struct sn_chunk {
  struct sn_chunk* next;
  struct sn_chunk *work_next, *work_previous;
  sn_work_meter* meter;
  size_t used, capacity;
  max_align_t alignment;
  unsigned char data[];
};
/* The parse-scoped, thread-local meter charges scanner/record/tree loop
 * iterations, byte-reader claims (including skipped arrays), comparisons,
 * sorting n*ceil(log2(n)), allocation requests and bulk text/expansion bytes.
 * Each loop iteration costs at least one unit even when it consumes no bytes.
 * Shared block slices charge only their length word; their decoders charge the
 * payload on use. A failed charge never increments past the ceiling. The escape
 * boundary owns a ledger of every live arena chunk, including scratch, and the
 * parse owner. Cleanup does not charge work. Nested parses restore the
 * enclosing meter. Standalone bitmap codes/raster calls have one input and a
 * pixel ceiling and do not establish a meter. */
_Thread_local sn_work_meter* sn_meter;
void sn_work(size_t bytes) { sn_work_on(sn_meter, bytes); }
void sn_work_release(sn_work_meter* meter, bool discard) {
  sn_chunk* chunk = meter->chunks;
  while (chunk) {
    sn_chunk* next = chunk->work_next;
    chunk->meter = nullptr;
    if (discard) free(chunk);
    chunk = next;
  }
  meter->chunks = nullptr;
  if (discard) free(meter->owner);
  meter->owner = nullptr;
}
int sn_work_memcmp(const void* a, const void* b, size_t size) {
  sn_work(size);
  return memcmp(a, b, size);
}
/* In-place heapsort avoids escaping through a libc comparator that might
 * own hidden scratch storage. Charge n*ceil(log2(n)), every comparison and
 * every byte swap; the worst-case traversal is logarithmic. */
static void swap_bytes(unsigned char* a, unsigned char* b, size_t width) {
  sn_work(width);
  for (size_t index = 0; index < width; index++) {
    unsigned char byte = a[index];
    a[index] = b[index];
    b[index] = byte;
  }
}
static void sift(unsigned char* base, size_t root, size_t count, size_t width,
                 int (*compare)(const void*, const void*)) {
  while (root < count / 2) {
    sn_work(1);
    size_t child = root * 2 + 1;
    if (child + 1 < count &&
        compare(base + child * width, base + (child + 1) * width) < 0)
      child++;
    if (compare(base + root * width, base + child * width) >= 0) break;
    swap_bytes(base + root * width, base + child * width, width);
    root = child;
  }
}
void sn_work_sort(void* base, size_t count, size_t width,
                  int (*compare)(const void*, const void*)) {
  for (size_t left = count; left > 1; left = left / 2 + left % 2)
    sn_work(count);
  for (size_t root = count / 2; root > 0; root--)
    sift(base, root - 1, count, width, compare);
  for (size_t left = count; left > 1; left--) {
    sn_work(1);
    swap_bytes(base, (unsigned char*)base + (left - 1) * width, width);
    sift(base, 0, left - 1, width, compare);
  }
}
#ifdef SNNOTE_TESTING
static _Thread_local size_t allocations_remaining = SIZE_MAX;
static _Thread_local bool allocation_failed;
static _Thread_local size_t arena_peak;
static _Thread_local size_t work_used, work_limit;
size_t sn_test_work_used(void) { return work_used; }
size_t sn_test_work_limit(void) { return work_limit; }
void sn_test_work_finish(const sn_work_meter* meter) {
  work_used = meter->used;
  work_limit = meter->limit;
}
static _Thread_local size_t cache_probes, decodes, duplicate_decodes;
static _Thread_local size_t kind_decodes[SN_CACHE_FIELDS + 1];
_Thread_local size_t sn_test_metadata_comparisons;
void sn_test_work_reset(void) {
  sn_work_memset(kind_decodes, 0, sizeof kind_decodes);
  cache_probes = sn_test_metadata_comparisons = decodes = duplicate_decodes = 0;
}
size_t sn_test_decodes(void) { return decodes; }
size_t sn_test_kind_decodes(sn_cache_kind kind) { return kind_decodes[kind]; }
size_t sn_test_duplicate_decodes(void) { return duplicate_decodes; }
size_t sn_test_cache_probes(void) { return cache_probes; }
size_t sn_test_arena_peak(void) { return arena_peak; }
void sn_test_peak_reset(void) { arena_peak = 0; }

/* Fail exactly one allocation, independently on each test thread. */
void sn_test_alloc_fail_after(size_t count) {
  allocations_remaining = count;
  allocation_failed = false;
}

/* Report whether the current test thread reached its injected failure boundary.
 */
bool sn_test_alloc_did_fail(void) { return allocation_failed; }
#endif

/* The owner and arena share the injection point so every parse allocation
 * can fail, including the one made before an arena exists. */
void* sn_calloc(size_t count, size_t width) {
#ifdef SNNOTE_TESTING
  if (allocations_remaining != SIZE_MAX) {
    if (allocations_remaining == 0) {
      allocations_remaining = SIZE_MAX;
      allocation_failed = true;
      return nullptr;
    }
    allocations_remaining--;
  }
#endif
  return calloc(count, width);
}

/* Each logical request passes the failure hook, including requests served
 * from a chunk. Abandoned head space is less than the next request (and
 * at most 64 KiB); its entire capacity remains charged. Empty arrays borrow a
 * shared aligned sentinel and cost nothing.
 */
void* sn_alloc(sn_arena* arena, size_t count, size_t width) {
  static max_align_t empty;
  if (arena->failed) return nullptr;
  if (!count || !width) return &empty;
#ifdef SNNOTE_TESTING
  if (allocations_remaining != SIZE_MAX) {
    if (!allocations_remaining) {
      allocations_remaining = SIZE_MAX;
      allocation_failed = arena->failed = true;
      return nullptr;
    }
    allocations_remaining--;
  }
#endif
  size_t alignment = alignof(max_align_t);
  if (count > (SIZE_MAX - sizeof(sn_chunk) - alignment) / width) {
    if (arena->budget) arena->budget->exceeded = true;
    arena->failed = true;
    return nullptr;
  }
  size_t bytes = count * width;
  sn_work(bytes);
  bytes = (bytes + alignment - 1) / alignment * alignment;
  sn_chunk* chunk = arena->head;
  if (!chunk || bytes > chunk->capacity - chunk->used) {
    size_t capacity = bytes > 65536 ? bytes : 65536;
    size_t total = sizeof(sn_chunk) + capacity;
    if (arena->budget && total > arena->budget->limit - arena->budget->live) {
      arena->budget->exceeded = arena->failed = true;
      return nullptr;
    }
    chunk = calloc(1, total);
    if (!chunk) {
      arena->failed = true;
      return nullptr;
    }
    chunk->meter = sn_meter;
    if (sn_meter) {
      chunk->work_next = sn_meter->chunks;
      if (chunk->work_next) chunk->work_next->work_previous = chunk;
      sn_meter->chunks = chunk;
    }
    chunk->capacity = capacity;
    chunk->next = arena->head;
    arena->head = chunk;
    if (arena->budget) {
      arena->budget->live += total;
      if (arena->budget->live > arena->budget->peak)
        arena->budget->peak = arena->budget->live;
#ifdef SNNOTE_TESTING
      arena_peak = arena->budget->peak;
#endif
    }
  }
  void* result = chunk->data + chunk->used;
  chunk->used += bytes;
  return result;
}

/* Release chunk capacity, including headers, from the shared live budget. */
void sn_arena_free(sn_arena* arena) {
  sn_chunk* chunk = arena->head;
  while (chunk) {
    sn_chunk* next = chunk->next;
    if (arena->budget) arena->budget->live -= sizeof *chunk + chunk->capacity;
    if (chunk->meter) {
      if (chunk->work_previous)
        chunk->work_previous->work_next = chunk->work_next;
      else
        chunk->meter->chunks = chunk->work_next;
      if (chunk->work_next)
        chunk->work_next->work_previous = chunk->work_previous;
    }
    free(chunk);
    chunk = next;
  }
  *arena = (sn_arena){0};
}

void sn_scratch_free(sn_arena* arena, sn_arena* scratch) {
  arena->failed |= scratch->failed;
  sn_arena_free(scratch);
}

/* AVL index: each (file offset, interpretation kind, numeric context) is
 * decoded at most once. Entries are never refused or evicted; allocation
 * failure aborts parsing. Callers cache negative interpretations as well.
 * Child pointers are topology only and never participate in key comparison.
 * Protocol entries instead use length and borrowed name bytes as their key.
 * All nodes and values are arena allocations charged to the parse budget. */
struct sn_cache {
  size_t key, kind, context;
  struct sn_cache *left, *right;
  int height;
  max_align_t alignment;
  unsigned char value[];
};

size_t sn_offset(const sn_arena* arena, const uint8_t* data) {
  /* Only slices inside the owned file (including its empty end slice) have
   * a cache identity. Integer range checks avoid unrelated-pointer subtraction.
   * SIZE_MAX bypasses the index; callers still decode under the work meter. */
  uintptr_t base = (uintptr_t)arena->file_base, address = (uintptr_t)data;
  if (!base || !data || address < base || address - base > arena->file_size)
    return SIZE_MAX;
  return (size_t)(address - base);
}
static int compare_key(size_t key, size_t kind, size_t context,
                       const sn_cache* node) {
#ifdef SNNOTE_TESTING
  cache_probes++;
#endif
  sn_work(1);
  if (kind != node->kind) return kind < node->kind ? -1 : 1;
  if (key != node->key) return key < node->key ? -1 : 1;
  if (kind == SN_CACHE_PROTOCOL)
    return key ? sn_work_memcmp((const void*)(uintptr_t)context,
                                (const void*)(uintptr_t)node->context, key)
               : 0;
  return (context > node->context) - (context < node->context);
}
static int height(const sn_cache* node) { return node ? node->height : 0; }
static void update_height(sn_cache* node) {
  int left = height(node->left), right = height(node->right);
  node->height = 1 + (left > right ? left : right);
}
static sn_cache* rotate_left(sn_cache* node) {
  sn_cache* root = node->right;
  node->right = root->left;
  root->left = node;
  update_height(node);
  update_height(root);
  return root;
}
static sn_cache* rotate_right(sn_cache* node) {
  sn_cache* root = node->left;
  node->left = root->right;
  root->right = node;
  update_height(node);
  update_height(root);
  return root;
}
static sn_cache* insert(sn_cache* root, sn_cache* entry) {
  if (!root) return entry;
  if (compare_key(entry->key, entry->kind, entry->context, root) < 0)
    root->left = insert(root->left, entry);
  else
    root->right = insert(root->right, entry);
  update_height(root);
  if (height(root->left) - height(root->right) > 1) {
    if (height(root->left->right) > height(root->left->left))
      root->left = rotate_left(root->left);
    return rotate_right(root);
  }
  if (height(root->right) - height(root->left) > 1) {
    if (height(root->right->left) > height(root->right->right))
      root->right = rotate_right(root->right);
    return rotate_left(root);
  }
  return root;
}
void* sn_cached(sn_arena* arena, size_t key, size_t kind, size_t context) {
  if (key == SIZE_MAX) return nullptr;
  sn_cache* node = arena->cache;
  while (sn_work(1), (node)) {
    int order = compare_key(key, kind, context, node);
    if (!order) return node->value;
    node = order < 0 ? node->left : node->right;
  }
  return nullptr;
}
#ifdef SNNOTE_TESTING
/* Count actual decoder entries, not cache hits or insertions. The completed
 * index is the ledger: a second entry for a completed tuple is a violation.
 * Negative annotation results live under INVALID with their kind as context.
 * Instrumentation lookups are excluded from the algorithm's step counter. */
void sn_decode_begin(sn_arena* arena, size_t key, sn_cache_kind kind,
                     size_t context) {
  if (arena->failed || key == SIZE_MAX) return;
  size_t steps = cache_probes;
  sn_work_meter* saved = sn_meter;
  sn_meter = nullptr;
  bool repeated = sn_cached(arena, key, kind, context) != nullptr;
  if (kind == SN_CACHE_TITLE || kind == SN_CACHE_KEYWORD ||
      kind == SN_CACHE_LINK)
    repeated |=
        sn_cached(arena, key, SN_CACHE_INVALID, (size_t)kind) != nullptr;
  cache_probes = steps;
  sn_meter = saved;
  decodes++;
  kind_decodes[kind]++;
  if (repeated) duplicate_decodes++;
}
#endif
void sn_cache_put(sn_arena* arena, size_t key, size_t kind, size_t context,
                  const void* value, size_t size) {
  if (arena->failed || key == SIZE_MAX) return;
  if (sn_cached(arena, key, kind, context)) {
#ifdef SNNOTE_TESTING
    duplicate_decodes++;
#endif
    return;
  }
  if (size > SIZE_MAX - sizeof(sn_cache)) {
    arena->failed = true;
    return;
  }
  sn_cache* entry = sn_alloc(arena, 1, sizeof *entry + size);
  if (!entry) return;
  entry->key = key;
  entry->kind = kind;
  entry->context = context;
  entry->height = 1;
  sn_work_memcpy(entry->value, value, size);
  arena->cache = insert(arena->cache, entry);
  arena->cache_count++;
}
/* Whole short names share the AVL index, ordered by length then bytes.
 * For C total cache entries, lookup/insertion visits O(log(C + 1)) nodes
 * and compares at most SN_PROTOCOL_MAX_LENGTH bytes per node, even for
 * adversarial names. Each distinct name adds one arena-allocated cache node
 * and size_t ID; no name bytes are copied. Node visits, compared bytes and
 * allocations consume the unchanged parse work/storage budgets. There is no
 * separate protocol-count cap: these resource budgets bound the population.
 * IDs follow first encounter, independently of tree order and rotations.
 * Long names bypass interning and keep their original stored text. */
size_t sn_protocol(sn_arena* arena, sn_string protocol) {
  if (protocol.length > SN_PROTOCOL_MAX_LENGTH) return SIZE_MAX;
  size_t context = (size_t)(uintptr_t)protocol.data;
  const size_t* cached =
      sn_cached(arena, protocol.length, SN_CACHE_PROTOCOL, context);
  if (cached) return *cached;
  size_t id = arena->protocol_count + 1;
  sn_cache_put(arena, protocol.length, SN_CACHE_PROTOCOL, context, &id,
               sizeof id);
  if (arena->failed) return SIZE_MAX;
  arena->protocol_count = id;
  return id;
}
#ifdef SNNOTE_TESTING
static bool walk(const sn_cache* node, const sn_cache** previous,
                 size_t* count) {
  if (!node) return true;
  if (!walk(node->left, previous, count)) return false;
  if (*previous && compare_key((*previous)->key, (*previous)->kind,
                               (*previous)->context, node) >= 0)
    return false;
  int left = height(node->left), right = height(node->right);
  if (abs(left - right) > 1 ||
      node->height != 1 + (left > right ? left : right))
    return false;
  *previous = node;
  (*count)++;
  return walk(node->right, previous, count);
}
bool sn_test_cache_walk(sn_arena* arena) {
  const sn_cache* previous = nullptr;
  size_t count = 0;
  return walk(arena->cache, &previous, &count) && count == arena->cache_count;
}
#endif

/* Impossible standalone declarations are malformed, not accumulated budget
 * exhaustion. Call before allocating storage driven by a declared count. */
bool sn_declared_fits(const sn_arena* arena, size_t count, size_t width) {
  size_t limit = arena->budget ? arena->budget->limit : SIZE_MAX;
  return !width || count <= limit / width;
}

/* Copy text with maximal-invalid-subpart UTF-8 repair. Reserve the worst-case
 * three-byte replacement plus a terminator before writing. */
sn_string sn_text(sn_arena* arena, const uint8_t* input, size_t length) {
  sn_work_meter* meter = sn_meter;
  if (length > (SIZE_MAX - 1) / 3) {
    arena->failed = true;
    return (sn_string){0};
  }
  char* out = sn_alloc(arena, length * 3 + 1, 1);
  if (!out) return (sn_string){0};
  size_t index = 0, written = 0;
  while (sn_work_on(meter, 1), (index < length)) {
    uint8_t lead = input[index];
    size_t len = 1;
    if (lead < 0x80) {
      out[written++] = (char)lead;
      index++;
      continue;
    }
    if (lead >= 0xc2 && lead <= 0xdf)
      len = 2;
    else if (lead >= 0xe0 && lead <= 0xef)
      len = 3;
    else if (lead >= 0xf0 && lead <= 0xf4)
      len = 4;
    size_t used = 1;
    if (len > 1) {
      while (sn_work_on(meter, 1), (used < len && used < length - index)) {
        uint8_t continuation_byte = input[index + used];
        if (continuation_byte < 0x80 || continuation_byte > 0xbf) break;
        if (used == 1 && ((lead == 0xe0 && continuation_byte < 0xa0) ||
                          (lead == 0xed && continuation_byte >= 0xa0) ||
                          (lead == 0xf0 && continuation_byte < 0x90) ||
                          (lead == 0xf4 && continuation_byte >= 0x90)))
          break;
        used++;
      }
    }
    if (len > 1 && used == len) {
      sn_work_memcpy(out + written, input + index, len);
      written += len;
    } else {
      sn_work_memcpy(out + written, "\xef\xbf\xbd", 3);
      written += 3;
    }
    index += used;
  }
  out[written] = 0;
  return (sn_string){out, written};
}

/* Compare a length-delimited string bytewise with a literal; absent text never
 * matches. */
bool sn_equal(sn_string text, const char* literal) {
  return text.data && text.length == strlen(literal) &&
         sn_work_memcmp(text.data, literal, text.length) == 0;
}

/* Interpret the metadata sentinels empty, none and zero as absence. */
sn_string sn_unset(sn_string text) {
  return !text.length || sn_equal(text, "none") || sn_equal(text, "0")
             ? (sn_string){0}
             : text;
}

/* Trim the format whitespace set by Unicode scalar, optionally including line
 * separators. The returned slice borrows the original bytes. */
sn_string sn_trim(sn_string text, bool newlines) {
  sn_work_meter* meter = sn_meter;
  size_t start = 0, kept = 0, index = 0;
  bool leading = true;
  while (sn_work_on(meter, 1), (index < text.length)) {
    uint32_t codepoint = (unsigned char)text.data[index++];
    unsigned continuation = codepoint >= 0xf0   ? 3
                            : codepoint >= 0xe0 ? 2
                            : codepoint >= 0xc0 ? 1
                                                : 0;
    if (continuation) {
      codepoint &= (1u << (6 - continuation)) - 1;
      for (unsigned entry_index = 0; sn_work_on(meter, 1),
                    (entry_index < continuation && index < text.length);
           entry_index++)
        codepoint =
            (codepoint << 6) | ((unsigned char)text.data[index++] & 63u);
    }
    bool white =
        codepoint == 9 || codepoint == 32 || codepoint == 0xa0 ||
        codepoint == 0x1680 || (codepoint >= 0x2000 && codepoint <= 0x200b) ||
        codepoint == 0x202f || codepoint == 0x205f || codepoint == 0x3000;
    if (newlines &&
        ((codepoint >= 10 && codepoint <= 13) || codepoint == 0x85 ||
         codepoint == 0x2028 || codepoint == 0x2029))
      white = true;
    if (leading && white) start = index;
    if (!white) {
      leading = false;
      kept = index;
    }
  }
  if (kept < start) kept = start;
  return (sn_string){text.data ? text.data + start : nullptr, kept - start};
}

/* Accumulate a signed decimal only after checking the next digit against its
 * limit. Failure leaves the output unchanged. */
bool sn_integer(sn_string text, int64_t* out) {
  sn_work_meter* meter = sn_meter;
  if (!text.data || !text.length) return false;
  size_t index = 0, end = text.length;
  bool neg = false;
  if (index < end && (text.data[index] == '-' || text.data[index] == '+'))
    neg = text.data[index++] == '-';
  if (index == end) return false;
  uint64_t value = 0, limit = neg ? (uint64_t)INT64_MAX + 1 : INT64_MAX;
  for (; sn_work_on(meter, 1), (index < end); index++) {
    unsigned character = (unsigned char)text.data[index];
    if (character < '0' || character > '9') return false;
    character -= '0';
    if (value > (limit - character) / 10) return false;
    value = value * 10 + character;
  }
  *out = neg ? (value == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)value)
             : (int64_t)value;
  return true;
}
/* Diagnostics contain format locations and counts, never stored text. */
sn_status sn_error_at(sn_error* error, sn_status status, int64_t value,
                      const char* detail) {
  if (error) {
    error->status = status;
    error->value = value;
    snprintf(error->message, sizeof error->message, "%s: %s", detail,
             sn_status_string(status));
  }
  return status;
}

/* Bitmap calls use a fixed format context; whole-file calls supply the
 * current block or key explicitly through sn_error_at. */
sn_status sn_error_set(sn_error* error, sn_status status, int64_t value) {
  const char* detail = "RATTA_RLE bitmap";
  switch (status) {
    case SN_OK:
      detail = "complete";
      break;
    case SN_ERR_INVALID_ARGUMENT:
      detail = "bitmap arguments or dimensions";
      break;
    case SN_ERR_OUT_OF_MEMORY:
      detail = "bitmap output allocation";
      break;
    case SN_ERR_IMAGE_ENCODING:
      detail = "bitmap PNG/JPEG encoding";
      break;
    case SN_ERR_UNSUPPORTED_ENCODING:
      detail = "bitmap protocol";
      break;
    case SN_ERR_LIMIT:
      detail = "bitmap pixel count exceeds ceiling";
      break;
    case SN_ERR_MALFORMED:
      detail = "RATTA_RLE runs do not fill bitmap dimensions";
      break;
    default:
      break;
  }
  return sn_error_at(error, status, value, detail);
}

void* sn_work_memcpy(void* out, const void* input, size_t size) {
  sn_work(size);
  return memcpy(out, input, size);
}
void* sn_work_memset(void* out, int value, size_t size) {
  sn_work(size);
  return memset(out, value, size);
}

void* sn_work_memmove(void* out, const void* input, size_t size) {
  sn_work(size);
  return memmove(out, input, size);
}
