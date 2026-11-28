#pragma once

// Task-side application data belongs in external RAM. These allocations must
// never be used by cache-disabled ISRs or by peripherals requiring internal DMA.
// Exhausting PSRAM must not silently consume the system's internal reserve.
#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace freeink::memory {
// Call after boot-critical peripherals have allocated their internal buffers.
// Ordinary task-side library allocations must leave SRAM for FreeRTOS locks
// and stacks. Explicit internal/DMA capability allocations are unaffected.
inline void preferExternalHeap() {
#if defined(ESP_PLATFORM) && !defined(FREEINK_HOST)
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) heap_caps_malloc_extmem_enable(0);
#endif
}
inline void* reallocate(void* pointer, size_t size) {
  if (!size) {
    free(pointer);
    return nullptr;
  }
  return heap_caps_realloc(pointer, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
inline void* allocate(size_t size) { return reallocate(nullptr, size); }

template <class T>
struct Destroy {
  void operator()(T* pointer) const {
    if (pointer) {
      std::destroy_at(pointer);
      free(pointer);
    }
  }
};
template <class T>
using Pointer = std::unique_ptr<T, Destroy<T>>;

// Boot-critical structures: callers use make() when allocation failure can be
// recovered. No constructor runs until the platform has initialized PSRAM.
template <class T, class... Args>
Pointer<T> make(Args&&... args) {
  void* storage = allocate(sizeof(T));
  if (!storage) return {};
  new (storage) T{std::forward<Args>(args)...};
  return Pointer<T>(static_cast<T*>(storage));
}
template <class T, class... Args>
Pointer<T> required(Args&&... args) {
  auto result = make<T>(std::forward<Args>(args)...);
  if (!result) {
    Serial.println("[Memory] Required PSRAM allocation failed");
    abort();
  }
  return result;
}
}  // namespace freeink::memory
