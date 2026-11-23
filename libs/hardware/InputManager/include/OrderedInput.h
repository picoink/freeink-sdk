#pragma once
#include <stdint.h>

namespace freeink {
// One ordered stream for applications that must preserve gestures across slow
// rendering. Reset explicitly cancels an incomplete stream after overflow.
struct OrderedInput {
  enum Kind : uint8_t { Key, Home, Tap, Swipe, Reset };
  Kind kind = Reset;
  uint8_t button = 0;
  bool pressed = false;
  uint32_t timestamp = 0, sequence = 0, dropped = 0;
  float x = 0, y = 0, x1 = 0, y1 = 0;
};
}
