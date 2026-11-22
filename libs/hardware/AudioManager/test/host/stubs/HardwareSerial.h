#pragma once
#include <cstddef>
#include <cstring>
#include <string>
#define SERIAL_8N1 0
inline std::string uartCommands;
inline int uartBegins = 0, uartEnds = 0;
inline bool uartFail = false;
inline std::string uartReply = "SET MODE 1\r\n", uartPending;
inline size_t uartReadPos = 0;
class HardwareSerial {
 public:
  explicit HardwareSerial(int n) { if (n != (FREEINK_DEVICE_METALIO_EINK4_PLUS ? 1 : 2)) __builtin_abort(); }
  void begin(int baud, int, int rx, int tx) {
    if (baud != 115200 || rx != (FREEINK_DEVICE_METALIO_EINK4_PLUS ? 38 : 47) || tx != (FREEINK_DEVICE_METALIO_EINK4_PLUS ? 39 : 48)) __builtin_abort();
    ++uartBegins;
  }
  int available() { return uartPending.size() - uartReadPos; }
  int read() { return uartPending.at(uartReadPos++); }
  size_t print(const char* text) { if (strstr(text, "AT+MODE")) { uartPending = uartReply; uartReadPos = 0; } uartCommands += text; return uartFail ? 0 : strlen(text); }
  void flush() {}
  void end() { ++uartEnds; }
};
