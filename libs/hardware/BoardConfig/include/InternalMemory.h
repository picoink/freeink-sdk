#pragma once
#include <esp_heap_caps.h>
#include <cstddef>
#include <mutex>
#include <new>
namespace freeink::resources {
// Admission for optional task stacks. Persistent DMA buffers and the firmware
// workers are established before applications can request radio/network work.
// Ordinary application data must use ResourceMemory, never this internal pool.
class InternalMemory {
 public:
  static constexpr size_t reserve=16*1024,taskOverhead=1024;
  static InternalMemory& get(){alignas(InternalMemory) static unsigned char storage[sizeof(InternalMemory)];static auto* p=new(storage) InternalMemory;return *p;}
  template<class Create> bool createTask(size_t stack,Create create) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto free=heap_caps_get_free_size(caps);
    if(stack>SIZE_MAX-taskOverhead-reserve || free<stack+taskOverhead+reserve ||
       heap_caps_get_largest_free_block(caps)<stack+taskOverhead){++denied_;return false;}
    if(!create()){++denied_;return false;}return true;
  }
  size_t denied(){std::lock_guard<std::mutex> lock(mutex_);return denied_;}
 private:
  static constexpr unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  std::mutex mutex_;size_t denied_=0;
};
}
