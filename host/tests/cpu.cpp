#include <cstdint>
extern "C" void cpu_work() {
  volatile uint64_t value = 1;
  for (uint64_t i = 0; i < 3000000; ++i) value = value * 1664525 + 1013904223;
}
