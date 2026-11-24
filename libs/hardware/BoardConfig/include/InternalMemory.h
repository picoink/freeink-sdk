#pragma once
#include <esp_heap_caps.h>
#include <cstddef>
#include <mutex>
#include <new>
#include <atomic>
#if defined(ESP_PLATFORM) && !defined(FREEINK_HOST) && !defined(FREEINK_TASK_TEST)
#include <esp_rom_sys.h>
#endif
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
  bool admitRadio() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Measured BTDM startup is larger than the steady-state controller. DMA
    // and ordinary internal SRAM are distinct pools on S31; both must fit.
    constexpr unsigned dmaCaps=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT;
    const bool ready=heap_caps_get_free_size(dmaCaps)>=112*1024 &&
        heap_caps_get_largest_free_block(dmaCaps)>=32*1024 &&
        heap_caps_get_free_size(caps)>=128*1024;
    if(!ready)++radioDenied_;return ready;
  }
  size_t radioDenied(){std::lock_guard<std::mutex> lock(mutex_);return radioDenied_;}
  // Heap failure hooks must not allocate or take heap locks.
  static void allocationFailed(size_t bytes,uint32_t caps,const char*) {
    auto& m=get();m.failedBytes_=bytes;m.failedCaps_=caps;++m.heapFailures_;
#if defined(ESP_PLATFORM) && !defined(FREEINK_HOST) && !defined(FREEINK_TASK_TEST)
    esp_rom_printf("[Memory] allocation failed bytes=%u caps=0x%x\n",unsigned(bytes),unsigned(caps));
#endif
  }
  void observeFailures() {
#if defined(ESP_PLATFORM) && !defined(FREEINK_HOST)
    heap_caps_register_failed_alloc_callback(allocationFailed);
#endif
  }
  size_t heapFailures()const{return heapFailures_;}
  size_t failedBytes()const{return failedBytes_;}
  uint32_t failedCaps()const{return failedCaps_;}
 private:
  static constexpr unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  std::mutex mutex_;size_t denied_=0;
  size_t radioDenied_=0;
  std::atomic<size_t> heapFailures_{0},failedBytes_{0};
  std::atomic<uint32_t> failedCaps_{0};
};
}
