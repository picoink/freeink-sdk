#pragma once
#include <cstdlib>
#include <cassert>
constexpr unsigned MALLOC_CAP_DMA = 1, MALLOC_CAP_INTERNAL = 2, MALLOC_CAP_8BIT = 4;
inline bool allocationFails = false;
inline int dmaBuffers = 0;
inline void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned flags) {
  assert(alignment == 64 && bytes == 4096 && flags == 7);
  if (allocationFails) return nullptr;
  void* data = nullptr;
  if (posix_memalign(&data, alignment, bytes)) return nullptr;
  ++dmaBuffers; return data;
}
inline void heap_caps_free(void* data) { if (data) --dmaBuffers; free(data); }
