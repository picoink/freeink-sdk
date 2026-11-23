#pragma once
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <thread>
inline void delay(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
