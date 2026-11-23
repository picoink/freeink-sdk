#pragma once
#include <cstddef>
#include <cstdint>
class TwoWire {
 public:
  bool begin(int = 0, int = 1, unsigned = 100000) { return true; }
  void end() {}
  void setTimeOut(unsigned) {}
  void setClock(unsigned) {}
  void beginTransmission(uint8_t address) {
    address_ = address;
    used_ = 0;
  }
  size_t write(uint8_t value) {
    if (used_ < sizeof(tx_)) tx_[used_++] = value;
    return 1;
  }
  uint8_t endTransmission(bool = true);
  uint8_t requestFrom(uint8_t address, uint8_t count, uint8_t = true);
  int read();

 private:
  uint8_t address_ = 0, tx_[32]{}, used_ = 0, pointer_[128]{}, reading_ = 0;
};
inline TwoWire Wire;
