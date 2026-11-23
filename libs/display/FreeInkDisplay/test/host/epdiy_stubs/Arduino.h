#pragma once
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <thread>
inline void delay(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline uint32_t micros() { return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
