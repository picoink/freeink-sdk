#pragma once
#include <BoardConfig.h>
#include <RefreshMetrics.h>

#include "Arduino.h"
class EInkDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  EInkDisplay(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t) {}
  ~EInkDisplay();
  EInkDisplay(const EInkDisplay&) = delete;
  EInkDisplay& operator=(const EInkDisplay&) = delete;
  void begin();
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const { return getDisplayWidth() / 8; }
  uint32_t getBufferSize() const { return getDisplayWidthBytes() * getDisplayHeight(); }
  uint8_t* getFrameBuffer() const { return buffer_; }
  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool = false);
  void displayBufferAsync(RefreshMode mode = FAST_REFRESH);
  bool refreshBusy();
  freeink::RefreshMetrics lastRefreshMetrics() const { return {}; }
  void requestResync(uint8_t = 0) {}
  bool lastRefreshSucceeded() const { return !freeink::host::fault("display"); }
  void waitRefreshComplete();
  bool supportsGray8() const { return true; }
  bool displayGray8BufferAsync(const uint8_t*, uint16_t stride, RefreshMode mode = FAST_REFRESH);
  bool supportsAsyncRefresh() const { return true; }
  void deepSleep() {}
  void clearScreen(uint8_t v = 255) { memset(buffer_, v, getBufferSize()); }

 private:
  uint8_t* buffer_ = nullptr;
  uint64_t busyUntil_ = 0;
};
