#pragma once
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstddef>
#include <mutex>
namespace freeink::memory {
// Shared admission budget for optional PSRAM caches. Working frames and DMA
// transactions are owned separately and are never evicted through this class.
// Owners release cold entries on their own execution context before retrying.
class CacheBudget {
 public:
  static CacheBudget& instance() { static CacheBudget budget; return budget; }
  bool acquire(size_t bytes) {
    if (!bytes) return true;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    const auto largest=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (bytes>limit_-used_ || free<reserve_ || bytes>free-reserve_ || bytes>largest) return false;
    used_+=bytes; return true;
  }
  void release(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_); used_-=std::min(used_,bytes);
  }
  size_t used() { std::lock_guard<std::mutex> lock(mutex_); return used_; }
  static constexpr size_t limit_=4*1024*1024, reserve_=2*1024*1024;
 private:
  std::mutex mutex_;
  size_t used_=0;
};
}
