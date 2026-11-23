#pragma once
#include <SdFat.h>
#include <HostRuntime.h>
namespace freeink {
// Directory-backed simulator models ownership only; raw FAT I/O is covered by
// the production adapter's isolated host test.
class FlashUsbStorage : public FsBlockDeviceInterface {
 public:
  bool begin(const char* = "storage") { active_ = !host::fault("flash"); return active_; }
  void end() { active_ = false; }
  uint16_t sectorSize() const { return 4096; }
 private:
  bool active_ = false;
};
}
