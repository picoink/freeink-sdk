#include <UsbMassStorage.h>
#include <USB.h>
#include <USBMSC.h>
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
extern "C" bool tud_disconnect() { mounted = false; return true; }
extern "C" bool tud_connect() { return true; }
class Disk : public FsBlockDeviceInterface {
 public:
  std::array<uint8_t, 2048> bytes{};
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
    memcpy(data, bytes.data() + sector * 512, count * 512); return true;
  }
  bool writeSectors(Sector_t sector, const uint8_t* data, size_t count) override {
    if (fail || sector + count > 4) return false;
    memcpy(bytes.data() + sector * 512, data, count * 512); return true;
  }
};
int main() {
  Disk disk;
  UsbMassStorage usb, second;
  assert(!usb.begin(nullptr));
  mscBeginOk = false; assert(!usb.begin(&disk) && !media); mscBeginOk = true;
  usbBeginOk = false; assert(!usb.begin(&disk) && !media); usbBeginOk = true;
  assert(usb.begin(&disk) && !second.begin(&disk));
  mounted = true;
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
  { std::lock_guard<std::mutex> lock(disk.mutex); disk.block = false; disk.cv.notify_all(); }
  assert(read.get() == 512); stop.get();
  assert(readCallback(0, 0, result.data(), 512) == -1);
  assert(second.begin(&disk)); second.end();
  puts("MSC ranges, partial sectors, failures, eject, re-entry and concurrent callback drain passed");
}
