#include "FlashUsbStorage.h"
#if FREEINK_CAP_USB_MSC && defined(FREEINK_INTERNAL_FAT)
#include <esp_partition.h>
namespace freeink {
bool FlashUsbStorage::begin(const char* label) {
  if (handle_ != WL_INVALID_HANDLE) return false;
  const auto* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_FAT,label);
  if (!part || wl_mount(part,&handle_) != ESP_OK) return false;
  const size_t size = wl_sector_size(handle_);
  if (size != 4096 || wl_size(handle_) % size) { end(); return false; }
  sectorSize_ = size; sectors_ = wl_size(handle_)/size;
  return sectors_ != 0;
}
void FlashUsbStorage::end() {
  if (handle_ != WL_INVALID_HANDLE) wl_unmount(handle_);
  handle_ = WL_INVALID_HANDLE; sectors_ = 0; sectorSize_ = 0;
}
bool FlashUsbStorage::valid(Sector_t n, size_t count) const {
  return handle_ != WL_INVALID_HANDLE && count && n < sectors_ && count <= sectors_-n;
}
bool FlashUsbStorage::readSectors(Sector_t n, uint8_t* p, size_t count) {
  return p && valid(n,count) && wl_read(handle_,size_t(n)*sectorSize_,p,count*sectorSize_) == ESP_OK;
}
bool FlashUsbStorage::writeSectors(Sector_t n, const uint8_t* p, size_t count) {
  if (!p || !valid(n,count)) return false;
  const size_t offset = size_t(n)*sectorSize_, length = count*sectorSize_;
  return wl_erase_range(handle_,offset,length) == ESP_OK && wl_write(handle_,offset,p,length) == ESP_OK;
}
}
#endif
