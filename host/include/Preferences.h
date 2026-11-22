#pragma once
#include "HostRuntime.h"
class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false);
  void end() {}
  uint8_t getUChar(const char* key, uint8_t value = 0) const;
  uint16_t getUShort(const char* key, uint16_t value = 0) const;
  bool getBool(const char* key, bool value = false) const;
  size_t putUChar(const char* key, uint8_t value);
  size_t putUShort(const char* key, uint16_t value);
  size_t putBool(const char* key, bool value);

 private:
  std::string path_;
  bool readOnly_ = false;
  freeink::host::Json values_{freeink::host::Object{}};
  size_t put(const char* key, freeink::host::Json value, size_t bytes);
};
