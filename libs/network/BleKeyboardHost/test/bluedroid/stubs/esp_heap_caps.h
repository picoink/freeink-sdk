#pragma once
#include <cstdlib>
#include <cstddef>
#define CONFIG_SPIRAM 1
constexpr unsigned MALLOC_CAP_8BIT=1, MALLOC_CAP_SPIRAM=2, MALLOC_CAP_INTERNAL=4;
inline bool failEventAllocation=false;
inline unsigned eventAllocationCaps=0;
inline size_t eventAllocationBytes=0;
inline void* heap_caps_malloc(size_t size,unsigned caps) {
  eventAllocationCaps=caps; eventAllocationBytes=size;
  return failEventAllocation ? nullptr : std::malloc(size);
}
