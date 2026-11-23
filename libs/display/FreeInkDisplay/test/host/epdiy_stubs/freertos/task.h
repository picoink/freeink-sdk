#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
struct FakeTask {
  std::mutex mutex;
  std::condition_variable cv;
  unsigned notifications = 0;
  std::thread thread;
};
using TaskHandle_t = FakeTask*;
inline thread_local FakeTask* currentTask = nullptr;
inline std::vector<std::unique_ptr<FakeTask>> tasks;
inline bool failTaskAllocation = false;
inline int xTaskCreatePinnedToCoreWithCaps(void (*fn)(void*), const char*, unsigned, void* ctx,
                                         unsigned, TaskHandle_t* handle, int, unsigned) {
  if (failTaskAllocation) return 0;
  auto task = std::make_unique<FakeTask>();
  *handle = task.get();
  task->thread = std::thread([fn, ctx, ptr = task.get()] { currentTask = ptr; fn(ctx); });
  tasks.push_back(std::move(task));
  return 1;
}
inline unsigned ulTaskNotifyTake(int, unsigned) {
  std::unique_lock<std::mutex> lock(currentTask->mutex);
  currentTask->cv.wait(lock, [] { return currentTask->notifications != 0; });
  unsigned count = currentTask->notifications;
  currentTask->notifications = 0;
  return count;
}
inline void xTaskNotifyGive(TaskHandle_t task) {
  std::lock_guard<std::mutex> lock(task->mutex);
  ++task->notifications;
  task->cv.notify_one();
}
inline void vTaskDeleteWithCaps(void*) {}
inline void joinTasks() {
  for (auto& task : tasks) task->thread.join();
  tasks.clear();
}
