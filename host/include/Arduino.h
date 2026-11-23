#pragma once
#include <algorithm>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "WString.h"
#include "esp_heap_caps.h"
#define PROGMEM
#define IRAM_ATTR
#define RTC_DATA_ATTR
#define SET_LOOP_TASK_STACK_SIZE(n)
inline unsigned uxTaskGetStackHighWaterMark(void*) { return 0; }
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_dword(p) (*(const uint32_t*)(p))
#define memcpy_P memcpy
#define F(s) s
constexpr int HIGH = 1, LOW = 0, INPUT = 1, OUTPUT = 3, INPUT_PULLUP = 5, INPUT_PULLDOWN = 6, FALLING = 2;
using TaskHandle_t = void*;
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
inline void delayMicroseconds(unsigned n) {
  if (n >= 1000) delay(n / 1000);
}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void yield() { delay(0); }
inline void* ps_malloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
inline void* ps_calloc(size_t n, size_t s) { return heap_caps_calloc(n, s, MALLOC_CAP_SPIRAM); }
inline void* ps_realloc(void* p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
class HardwareSerial {
 public:
  void begin(unsigned long) {}
  void setTxTimeoutMs(unsigned) {}
  explicit operator bool() const { return true; }
  int available() const { return 0; }
  int read() { return -1; }
  void flush() {}
  size_t write(const uint8_t* data, size_t size) { return std::fwrite(data, 1, size, stderr); }
  void print(const char* s) { std::fputs(s, stderr); }
  void println(const char* s = "") { std::fprintf(stderr, "%s\n", s); }
  void printf(const char* format, ...) {
    va_list a;
    va_start(a, format);
    std::vfprintf(stderr, format, a);
    va_end(a);
  }
};
inline HardwareSerial Serial, Serial0;
struct HostEsp {
  size_t getFreeHeap() const { return host_heap_free(MALLOC_CAP_INTERNAL); }
  size_t getFreePsram() const { return host_heap_free(MALLOC_CAP_SPIRAM); }
  size_t getPsramSize() const { return 16 * 1024 * 1024; }
  [[noreturn]] void restart();
};
inline HostEsp ESP;
