#pragma once
#include <cstdint>
inline constexpr int OUTPUT=1, LOW=0, HIGH=1;
inline int usbRoute=HIGH;
inline void pinMode(int,int) {}
inline void digitalWrite(int,int value) { usbRoute=value; }
