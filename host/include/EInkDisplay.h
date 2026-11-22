#pragma once
#include "Arduino.h"
#include <BoardConfig.h>
class EInkDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  EInkDisplay(int8_t,int8_t,int8_t,int8_t,int8_t,int8_t) {}
  void begin();
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const { return getDisplayWidth()/8; }
  uint32_t getBufferSize() const { return getDisplayWidthBytes()*getDisplayHeight(); }
  uint8_t* getFrameBuffer() const { return buffer_; }
  void displayBuffer(RefreshMode mode=FAST_REFRESH,bool=false);
  void displayBufferAsync(RefreshMode mode=FAST_REFRESH);
  bool refreshBusy();
  void waitRefreshComplete();
  bool supportsAsyncRefresh() const { return true; }
  void deepSleep() {}
  void clearScreen(uint8_t v=255) { memset(buffer_,v,getBufferSize()); }
 private:
  uint8_t* buffer_=nullptr;
  uint64_t busyUntil_=0;
};
