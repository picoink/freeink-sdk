#pragma once
#include "HostMemory.h"
#define MALLOC_CAP_8BIT 1u
#define MALLOC_CAP_INTERNAL 2u
#define MALLOC_CAP_SPIRAM 4u
#define MALLOC_CAP_DMA 8u
#define MALLOC_CAP_DEFAULT 16u
static inline void* heap_caps_malloc(size_t n, unsigned c) { return host_caps_malloc(n, c); }
static inline void* heap_caps_calloc(size_t n, size_t s, unsigned c) {
  if (s && n > SIZE_MAX / s) return 0;
  void* p = host_caps_malloc(n * s, c);
  if (p) __builtin_memset(p, 0, n * s);
  return p;
}
static inline void* heap_caps_realloc(void* p, size_t n, unsigned c) { return host_caps_realloc(p, n, c); }
static inline void heap_caps_free(void* p) { host_free(p); }
static inline size_t heap_caps_get_free_size(unsigned c) { return host_heap_free(c); }
static inline size_t heap_caps_get_largest_free_block(unsigned c) { return host_heap_largest(c); }
