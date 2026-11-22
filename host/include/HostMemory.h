#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void* host_malloc(size_t size);
void* host_calloc(size_t count, size_t size);
void* host_realloc(void* ptr, size_t size);
void host_free(void* ptr);
void* host_caps_malloc(size_t size, unsigned caps);
void* host_caps_realloc(void* ptr, size_t size, unsigned caps);
void* host_aligned_alloc(size_t alignment, size_t size);
size_t host_heap_free(unsigned caps);
size_t host_heap_largest(unsigned caps);
#ifdef __cplusplus
}
#include "HostRuntime.h"
namespace freeink::host {
void configureResources(const Json& settings);
Json resourceStats();
void cpuCheckpoint();
void failAllocation(const Json& settings);
extern thread_local bool firmwareAllocations;
struct HostScope {
  bool previous;
  HostScope() : previous(firmwareAllocations) { firmwareAllocations = false; }
  ~HostScope() { firmwareAllocations = previous; }
};
}  // namespace freeink::host
#endif
