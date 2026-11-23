#include <FlashUsbStorage.h>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
static esp_partition_t partition;
static std::array<uint8_t,4096*4> flash;
static bool mounted = false, fail = false;
static size_t sectorSize = 4096;
static int writes = 0, erases = 0;
const esp_partition_t* esp_partition_find_first(int type,int subtype,const char* label) {
  assert(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_FAT);
  return !strcmp(label,"storage") ? &partition : nullptr;
}
int wl_mount(const esp_partition_t* part,wl_handle_t* out) {
  assert(part == &partition && !mounted); mounted = true; *out = 1; return 0;
}
int wl_unmount(wl_handle_t handle) { assert(mounted && handle == 1); mounted = false; return 0; }
size_t wl_sector_size(wl_handle_t) { return sectorSize; }
size_t wl_size(wl_handle_t) { return flash.size(); }
int wl_read(wl_handle_t,size_t at,void* data,size_t n) {
  assert(at+n <= flash.size()); memcpy(data,flash.data()+at,n); return fail ? -1 : 0;
}
int wl_write(wl_handle_t,size_t at,const void* data,size_t n) {
  ++writes; assert(at+n <= flash.size()); memcpy(flash.data()+at,data,n); return fail ? -1 : 0;
}
int wl_erase_range(wl_handle_t,size_t at,size_t n) {
  ++erases; assert(at%4096 == 0 && n%4096 == 0 && at+n <= flash.size());
  if (fail) return -1;
  memset(flash.data()+at,0xff,n); return 0;
}
int main() {
  freeink::FlashUsbStorage disk;
  assert(!disk.begin("nvs"));
  sectorSize = 512; assert(!disk.begin() && !mounted);
  sectorSize = 4096; assert(disk.begin() && disk.sectorCount() == 4 && disk.sectorSize() == 4096);
  assert(!disk.begin());
  flash.fill(0x5a); std::array<uint8_t,4096> data, output; data.fill(0x37);
  assert(disk.writeSector(1,data.data()) && erases == 1 && writes == 1);
  assert(flash[4095] == 0x5a && flash[8192] == 0x5a);
  assert(disk.readSector(1,output.data()) && data == output);
  assert(!disk.writeSectors(3,data.data(),2) && !disk.readSector(UINT32_MAX,output.data()));
  fail = true; assert(!disk.writeSector(1,data.data()) && writes == 1);
  disk.end(); assert(!mounted && !disk.syncDevice() && !disk.writeSector(0,data.data()));
  fail = false; assert(disk.begin() && disk.readSector(1,output.data()) && output == data);
  disk.end();
  puts("Wear-levelled FAT geometry, partition isolation, bounds, erase failure and remount passed");
}
