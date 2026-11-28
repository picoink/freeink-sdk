#pragma once
#include <cstdlib>
#include <cassert>
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
inline bool heapFail=false;
inline size_t heapLive=0;
inline void* heap_caps_malloc(size_t size,unsigned caps) {
  assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(heapFail)return nullptr;
  void* p=std::malloc(size);if(p)++heapLive;return p;
}
inline void heap_caps_free(void* p){if(p){assert(heapLive);--heapLive;std::free(p);}}
