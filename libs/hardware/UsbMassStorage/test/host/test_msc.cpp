#include <UsbMassStorage.h>
#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <esp_heap_caps.h>
#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <future>
#include <mutex>
using namespace freeink;
using namespace std::chrono_literals;
static std::atomic<bool> mounted{false};
extern "C" bool tud_mounted() { return mounted; }
extern "C" bool tud_suspended() { return false; }
extern "C" bool tud_disconnect() { mounted=false; usbPullup=false; detachedMs=0; return true; }
extern "C" bool tud_connect() {
  if (FREEINK_DEVICE_METALIO_EINK4_PLUS) assert(usbRoute==LOW && detachedMs>=100 && !usbPullup);
  usbPullup=true;
  return true;
}
class Disk : public FsBlockDeviceInterface {
 public:
  std::array<uint8_t, 4*4096> bytes{};
  size_t sectorSize = 512;
  std::mutex mutex;
  std::condition_variable cv;
  bool block = false, entered = false, fail = false, syncOk = true;
  int syncs = 0;
  Sector_t sectorCount() override { return 4; }
  bool syncDevice() override { ++syncs; return syncOk; }
  bool readSectors(Sector_t sector, uint8_t* data, size_t count) override {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true; cv.notify_all(); cv.wait(lock, [&] { return !block; });
    if (fail || sector + count > 4) return false;
    memcpy(data, bytes.data() + sector * sectorSize, count * sectorSize); return true;
  }
  bool writeSectors(Sector_t sector, const uint8_t* data, size_t count) override {
    if (fail || sector + count > 4) return false;
    memcpy(bytes.data() + sector * sectorSize, data, count * sectorSize); return true;
  }
};
int main() {
  Disk disk;
  UsbMassStorage usb, second;
  assert(!usb.begin(nullptr));
  heapFail=true;assert(!usb.begin(&disk) && !heapLive);heapFail=false;
  mscBeginOk = false; assert(!usb.begin(&disk) && !media); mscBeginOk = true;
  assert(!heapLive);
  assert(usbRoute==HIGH);
  usbBeginOk = false; assert(!usb.begin(&disk) && !media); usbBeginOk = true;
  assert(!heapLive);
  assert(usbRoute==HIGH);
  assert(usb.begin(&disk) && !second.begin(&disk));
  assert(usbRoute==(FREEINK_DEVICE_METALIO_EINK4_PLUS ? LOW : HIGH));
  mounted = true;
  assert(usb.state()==UsbMassStorageState::WaitingForHost); // Stale previous-session flag.
  mounted = false;
  assert(usb.state()==UsbMassStorageState::WaitingForHost); // Delayed disconnect cannot end this session.
  mounted = true; mountEvent(nullptr,nullptr,ARDUINO_USB_STARTED_EVENT,nullptr);
  assert(usb.state() == UsbMassStorageState::Connected);
  std::array<uint8_t, 700> payload, result;
  for (size_t i = 0; i < payload.size(); ++i) payload[i] = uint8_t(i);
  disk.bytes.fill(0xAA);
  assert(writeCallback(0, 400, payload.data(), payload.size()) == 700);
  assert(readCallback(0, 400, result.data(), result.size()) == 700 && result == payload);
  assert(disk.bytes[399] == 0xAA && disk.bytes[1100] == 0xAA);
  assert(readCallback(UINT32_MAX, UINT32_MAX, result.data(), 700) == -1);
  assert(usb.state() == UsbMassStorageState::IoError);
  assert(readCallback(0, 0, result.data(), 0) == -1);
  usb.end(); assert(!media && readCallback(0, 0, result.data(), 512) == -1);
  if (FREEINK_DEVICE_METALIO_EINK4_PLUS) assert(settledStorageRoute);
  assert(!heapLive);
  assert(usbRoute==HIGH);
  assert(usb.begin(&disk)); mounted = true;
  disk.syncOk = false;
  assert(!stopCallback(0, false, true) && usb.state() == UsbMassStorageState::IoError);
  disk.syncOk = true;
  assert(stopCallback(0, false, true) && usb.state() == UsbMassStorageState::Ejected);
  assert(writeCallback(0, 0, payload.data(), 512) == -1);
  usb.end();
  assert(usb.begin(&disk)); mounted = true;
  disk.entered = false; disk.block = true;
  auto read = std::async(std::launch::async, [&] { return readCallback(0, 0, result.data(), 512); });
  { std::unique_lock<std::mutex> lock(disk.mutex); disk.cv.wait(lock, [&] { return disk.entered; }); }
  auto stop = std::async(std::launch::async, [&] { usb.end(); });
  assert(stop.wait_for(30ms) == std::future_status::timeout); // Must wait for the callback, even after disconnect.
  assert(usbRoute==(FREEINK_DEVICE_METALIO_EINK4_PLUS ? LOW : HIGH));
  { std::lock_guard<std::mutex> lock(disk.mutex); disk.block = false; disk.cv.notify_all(); }
  assert(read.get() == 512); stop.get();
  assert(usbRoute==HIGH);
  assert(readCallback(0, 0, result.data(), 512) == -1);
  assert(second.begin(&disk)); second.end();
  disk.sectorSize = 4096;
  assert(!usb.begin(&disk,1024));
  assert(usb.begin(&disk,4096) && advertisedSectorSize == 4096);
  disk.bytes.fill(0xA5);
  assert(writeCallback(0,4000,payload.data(),700) == 700);
  assert(readCallback(0,4000,result.data(),700) == 700 && result == payload);
  assert(disk.bytes[3999] == 0xA5 && disk.bytes[4700] == 0xA5);
  assert(readCallback(3,4000,result.data(),700) == -1);
  usb.end();
  puts("MSC ranges, partial sectors, failures, eject, re-entry and concurrent callback drain passed");
}
