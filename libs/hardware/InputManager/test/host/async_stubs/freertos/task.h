#pragma once
#include <freertos/FreeRTOS.h>
#include <atomic>
#include <chrono>
#include <thread>
inline bool failTask = false;
inline std::atomic<int> liveTasks{0};
inline std::atomic<bool> inTaskDelay{false};
inline void vTaskDelay(int ms) { inTaskDelay = true; std::this_thread::sleep_for(std::chrono::milliseconds(ms)); inTaskDelay = false; }
inline int xTaskCreate(void (*run)(void*), const char*, int, void* arg, int, void** handle) {
  if (failTask) return 0;
  *handle = reinterpret_cast<void*>(1); ++liveTasks;
  std::thread([=] { run(arg); --liveTasks; }).detach(); return 1;
}
inline void vTaskDelete(void*) {}
