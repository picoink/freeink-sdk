#pragma once
#include <cstdint>
inline constexpr int OUTPUT=1, LOW=0, HIGH=1;
inline int usbRoute=HIGH;
inline bool usbPullup=false;
inline unsigned detachedMs=0;
inline bool settledStorageRoute=false;
inline void pinMode(int,int) {}
inline void delay(unsigned ms) { if (!usbPullup && usbRoute==LOW) detachedMs+=ms; }
inline void digitalWrite(int,int value) {
  if (value==HIGH) settledStorageRoute=!usbPullup && detachedMs>=100;
  usbRoute=value;
  detachedMs=0;
}
