#pragma once
#include <cstdint>
#include <initializer_list>
constexpr int OUTPUT = 1, HIGH = 1, LOW = 0;
inline void delay(unsigned) {} inline unsigned long millis() { return 0; }
inline void pinMode(int, int) {} inline void digitalWrite(int, int) {}
struct SerialStub { explicit operator bool() { return false; } template<class... T> void printf(const char*, T...) {} };
inline SerialStub Serial;
