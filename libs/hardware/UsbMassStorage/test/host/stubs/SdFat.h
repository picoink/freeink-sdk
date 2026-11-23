#pragma once
#include <cstddef>
#include <cstdint>
using Sector_t = uint32_t;
class FsBlockDeviceInterface {
 public:
  virtual ~FsBlockDeviceInterface() = default;
  virtual void end() {}
  virtual bool isBusy() { return false; }
  virtual Sector_t sectorCount() = 0;
  virtual bool syncDevice() = 0;
  virtual bool readSectors(Sector_t, uint8_t*, size_t) = 0;
  virtual bool writeSectors(Sector_t, const uint8_t*, size_t) = 0;
  virtual bool readSector(Sector_t sector, uint8_t* data) { return readSectors(sector, data, 1); }
  virtual bool writeSector(Sector_t sector, const uint8_t* data) { return writeSectors(sector, data, 1); }
};
