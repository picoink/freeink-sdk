#pragma once
// Metalio E-INK4 Plus rev1.4 charger. Register fields and 4.4 V cell policy
// follow CloudZao/Metalio-E-INK4-Plus (7cfae83), common/power/cx25601n.cc.
// This IC is NOT register-compatible with the BQ25896. Never select that backend.
#include <MetalioEInk4PlusBoard.h>

namespace freeink {
class Cx25601n {
 public:
  struct Status {
    uint8_t charge = 0, vbus = 0;
    bool charging() const { return charge != 0; }
    bool externalPower() const { return vbus != 0 && vbus != 7; }
  };
  enum class ServiceResult { Ok, IoError, ShutdownRequested };

  static bool readStatus(Status& status) {
    uint8_t raw;
    if (!read(0x1E, raw)) return false;
    status = {uint8_t((raw >> 3) & 3), uint8_t(raw & 7)};
    return true;
  }
  bool present() const { return ready_; }
  bool begin(uint16_t currentMa = 1000) {
    ready_ = false;
    uint8_t part;
    if (!metalio_plus::ensureBooted() || !read(0x38, part)) return false;
    const I2cBusLock lock;
    // Leave charging disabled if any programming/verification step fails.
    bool ok = bits(0x16, 0x20, 0) && bits(0x18, 0x40, 0) && bits(0x15, 0x40, 0) &&
        bits(0x16, 0x13, 0) && bits(0x10, 0xF0, 0xC0) && bits(0x11, 1, 0) &&
        bits(0x12, 0xF8, 0x30) && bits(0x13, 1, 0) && setVoltage() && setCurrent(currentMa);
    bool unlocked = false;
    for (unsigned i = 0; ok && !unlocked && i < 10; ++i) {
      uint8_t value = 0;
      ok = write(0x70, 0) && write(0x70, 0x50) && write(0x70, 0x57) && write(0x70, 0x44) && read(0x70, value);
      unlocked = ok && value == 3;
    }
    ok = ok && unlocked && write(0x86, 6) && write(0x3A, 0x10) && write(0x46, 0x20);
    const bool relocked = write(0x70, 0);  // Always close the private window.
    ok = ok && relocked && bits(0x14, 1, 1) && bits(0x18, 4, 0) &&
        bits(0x1A, 0x80, 0x80) && bits(0x23, 0x1C, 0x1C) && bits(0x24, 8, 8) && bits(0x16, 0x20, 0x20);
    if (!ok) { write(0x70, 0); bits(0x16, 0x20, 0); }
    ready_ = ok;
    return ok;
  }
  // Quantize down: nominal 500/1000/1500 mA become 480/960/1440 mA.
  // Split register writes are made with charging disabled, verified, then restored.
  bool setCurrent(uint16_t ma) {
    if (ma < 80 || ma > 3040) return false;
    const I2cBusLock lock;
    uint8_t control;
    if (!read(0x16, control) || !bits(0x16, 0x20, 0)) return false;
    const unsigned code = ma / 80;
    const unsigned input = code * 80 < 100 ? 5 : (code * 4 > 150 ? 150 : code * 4);
    bool ok = bits(0x02, 0xC0, (code & 3) << 6) && bits(0x03, 0x0F, code >> 2) &&
        bits(0x06, 0xF0, (input & 15) << 4) && bits(0x07, 0x0F, input >> 4);
    uint16_t actual;
    ok = ok && getCurrent(actual) && actual == code * 80;
    if (ok) ok = bits(0x16, 0x20, control & 0x20);
    if (!ok) { ready_ = false; bits(0x16, 0x20, 0); }
    return ok;
  }
  bool getCurrent(uint16_t& ma) const {
    const I2cBusLock lock;
    uint8_t lo, hi;
    if (!read(2, lo) || !read(3, hi)) return false;
    ma = ((lo >> 6) | ((hi & 15) << 2)) * 80;
    return true;
  }
  // Call from the hardware owner periodically with fresh gauge data. No task is
  // suspended while holding I2C; the application can stop servicing before sleep.
  ServiceResult service(uint16_t mv, bool voltageKnown, uint16_t percent, bool percentKnown) {
    if (!ready_) return ServiceResult::IoError;
    Status status;
    int16_t current;
    if (!readStatus(status) || !readCurrent(current)) return ServiceResult::IoError;
    const bool valid = voltageKnown && mv >= 2500 && mv <= 4800;
    if (!status.externalPower() && status.vbus != 7 && !status.charging() && current <= 5 &&
        ((valid && mv <= 3100) || (percentKnown && percent == 0))) return ServiceResult::ShutdownRequested;
    // Missing gauge data must never disable termination or force a recharge.
    if (!valid) return bits(0x14, 4, 4) ? ServiceResult::Ok : ServiceResult::IoError;
    if (!setVoltage() || !bits(0x14, 4, mv < 4400 ? 0 : 4)) return ServiceResult::IoError;
    if (status.externalPower() && !status.charging() && current < 5 && mv < 4400) {
      if (!bits(0x16, 0x30, 0)) return ServiceResult::IoError;
      delay(200);  // No bus lock held while the charger settles.
      if (!bits(0x16, 0x30, 0x20)) return ServiceResult::IoError;
    }
    return ServiceResult::Ok;
  }
  // The caller must close storage and finish display/audio first.
  bool enterShipMode() { return ready_ && write(0x18, 1); }

 private:
  bool ready_ = false;
  static bool read(uint8_t reg, uint8_t& value) {
    const I2cBusLock lock;
    Wire.beginTransmission(0x6B); Wire.write(reg);
    if (Wire.endTransmission(false) || Wire.requestFrom(uint8_t(0x6B), uint8_t(1), uint8_t(true)) != 1) return false;
    value = Wire.read(); return true;
  }
  static bool write(uint8_t reg, uint8_t value) {
    const I2cBusLock lock;
    Wire.beginTransmission(0x6B); Wire.write(reg); Wire.write(value);
    return Wire.endTransmission() == 0;
  }
  static bool bits(uint8_t reg, uint8_t mask, uint8_t value) {
    const I2cBusLock lock;
    uint8_t previous, verify;
    return read(reg, previous) && write(reg, (previous & ~mask) | (value & mask)) &&
        read(reg, verify) && (verify & mask) == (value & mask);
  }
  static bool setVoltage() { // Fixed to the vendor rev1.4 high-voltage cell.
    const I2cBusLock lock;
    constexpr unsigned code = 4400 / 10;
    return bits(4, 0xF8, (code & 31) << 3) && bits(5, 0x0F, code >> 5);
  }
  static bool readCurrent(int16_t& ma) {
    const I2cBusLock lock;
    Wire.beginTransmission(0x55); Wire.write(0x0C);
    if (Wire.endTransmission(false) || Wire.requestFrom(uint8_t(0x55), uint8_t(2), uint8_t(true)) != 2) return false;
    const uint8_t lo = Wire.read();
    ma = int16_t(lo | (uint16_t(Wire.read()) << 8)); return true;
  }
};
}  // namespace freeink
