#pragma once
#include <BoardConfig.h>
#if FREEINK_CAP_USB_MSC && defined(FREEINK_INTERNAL_FAT)
#include <SdFat.h>
#include <wear_levelling.h>
namespace freeink {
// Raw access to an already provisioned FAT partition through the same wear
// levelling layer as FFat. The owner must unmount FFat before begin(), and end
// the MSC session before end(). Never formats or touches other partitions.
class FlashUsbStorage : public FsBlockDeviceInterface {
 public:
  bool begin(const char* label = "storage");
  void end() override;
  bool isBusy() override { return false; }
  uint16_t sectorSize() const { return sectorSize_; }
  Sector_t sectorCount() override { return sectors_; }
  bool syncDevice() override { return handle_ != WL_INVALID_HANDLE; }
  bool readSector(Sector_t n, uint8_t* p) override { return readSectors(n,p,1); }
  bool writeSector(Sector_t n, const uint8_t* p) override { return writeSectors(n,p,1); }
  bool readSectors(Sector_t, uint8_t*, size_t) override;
  bool writeSectors(Sector_t, const uint8_t*, size_t) override;
 private:
  bool valid(Sector_t n, size_t count) const;
  wl_handle_t handle_ = WL_INVALID_HANDLE;
  uint16_t sectorSize_ = 0;
  Sector_t sectors_ = 0;
};
}
#endif
