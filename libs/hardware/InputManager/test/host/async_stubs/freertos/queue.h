#pragma once
#include <freertos/FreeRTOS.h>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>
inline int queueCreates = 0, failQueueAt = 0;
inline std::atomic<int> liveQueues{0};
struct HostQueue { size_t capacity, itemSize; std::mutex mutex; std::deque<std::vector<uint8_t>> items; };
inline void* xQueueCreate(int capacity, int size) {
  if (++queueCreates == failQueueAt) return nullptr;
  ++liveQueues; return new HostQueue{size_t(capacity), size_t(size), {}, {}};
}
inline int xQueueSend(void* handle, const void* data, int) {
  auto& q = *static_cast<HostQueue*>(handle); std::lock_guard<std::mutex> lock(q.mutex);
  if (q.items.size() == q.capacity) return 0;
  const auto* bytes = static_cast<const uint8_t*>(data); q.items.emplace_back(bytes, bytes + q.itemSize); return 1;
}
inline int xQueueReceive(void* handle, void* data, int) {
  auto& q = *static_cast<HostQueue*>(handle); std::lock_guard<std::mutex> lock(q.mutex);
  if (q.items.empty()) return 0;
  memcpy(data, q.items.front().data(), q.itemSize); q.items.pop_front(); return 1;
}
inline void xQueueReset(void* handle) { auto& q = *static_cast<HostQueue*>(handle); std::lock_guard<std::mutex> lock(q.mutex); q.items.clear(); }
inline void vQueueDelete(void* handle) { delete static_cast<HostQueue*>(handle); --liveQueues; }
