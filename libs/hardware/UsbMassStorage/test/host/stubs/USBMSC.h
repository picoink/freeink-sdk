#pragma once
#include <cstdint>
inline int32_t (*readCallback)(uint32_t, uint32_t, void*, uint32_t);
inline int32_t (*writeCallback)(uint32_t, uint32_t, uint8_t*, uint32_t);
inline bool (*stopCallback)(uint8_t, bool, bool);
inline bool mscBeginOk = true, media = false;
inline uint16_t advertisedSectorSize = 0;
class USBMSC {
 public:
  void vendorID(const char*) {} void productID(const char*) {} void productRevision(const char*) {}
  void mediaPresent(bool value) { media = value; } void isWritable(bool) {}
  void onRead(decltype(readCallback) f) { readCallback = f; }
  void onWrite(decltype(writeCallback) f) { writeCallback = f; }
  void onStartStop(decltype(stopCallback) f) { stopCallback = f; }
  bool begin(uint32_t, uint16_t size) { advertisedSectorSize = size; return mscBeginOk; }
};
