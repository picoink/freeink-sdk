#include "UsbMassStorage.h"

#if FREEINK_CAP_USB_MSC

#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

extern "C" bool tud_mounted(void);
extern "C" bool tud_suspended(void);
extern "C" bool tud_disconnect(void);
extern "C" bool tud_connect(void);

namespace freeink {
namespace {

USBMSC gMsc;
std::mutex gIoMutex;
std::atomic<bool> gAccepting{false};
std::atomic<UsbMassStorage*> gOwner{nullptr};
std::atomic<FsBlockDeviceInterface*> gDev{nullptr};
uint16_t gBlockSize = 512;
uint8_t* gSectorScratch=nullptr;
bool gEventsRegistered=false;

void mountedEvent(void*,esp_event_base_t,int32_t,void*) {
  std::lock_guard<std::mutex> lock(gIoMutex);
  if (gAccepting.load()) if (auto* owner=gOwner.load()) owner->markConnected();
}

void routeStorage(bool active) {
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
  // The two PHYs share one cable. Expose a disconnected bus between device
  // identities so the host hub can debounce detach before enumerating again.
  if (!active) delay(250);
  pinMode(36, OUTPUT); digitalWrite(36, active ? LOW : HIGH);
  if (active) delay(250);
#else
  (void)active;
#endif
}

bool isRangeValid(FsBlockDeviceInterface* dev, const uint32_t lba, const uint32_t offset, const uint32_t bufsize) {
  if (!dev || bufsize == 0 || bufsize > INT32_MAX) return false;
  const uint64_t totalBytes = static_cast<uint64_t>(dev->sectorCount()) * gBlockSize;
  const uint64_t start = static_cast<uint64_t>(lba) * gBlockSize + offset;
  const uint64_t end = start + bufsize;
  return end >= start && end <= totalBytes;
}

int32_t mscRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  std::lock_guard<std::mutex> lock(gIoMutex);
  if (!gAccepting.load()) return -1;
  auto* const dev = gDev.load();
  auto* const owner = gOwner.load();
  if (!buffer || !isRangeValid(dev, lba, offset, bufsize)) {
    if (owner) owner->markIoError();
    return -1;
  }

  const uint64_t start = static_cast<uint64_t>(lba) * gBlockSize + offset;
  auto* output = static_cast<uint8_t*>(buffer);
  uint32_t remaining = bufsize;
  uint64_t cursor = start;

  if ((offset % gBlockSize) == 0 && (bufsize % gBlockSize) == 0) {
    const size_t sectors = bufsize / gBlockSize;
    if (!dev->readSectors(static_cast<Sector_t>(start / gBlockSize), output, sectors)) {
      if (owner) owner->markIoError();
      return -1;
    }
  } else {
    while (remaining > 0) {
      const auto sector = static_cast<Sector_t>(cursor / gBlockSize);
      const size_t within = static_cast<size_t>(cursor % gBlockSize);
      const size_t count = std::min<size_t>(gBlockSize - within, remaining);
      if (!dev->readSector(sector, gSectorScratch)) {
        if (owner) owner->markIoError();
        return -1;
      }
      memcpy(output, gSectorScratch + within, count);
      output += count;
      cursor += count;
      remaining -= count;
    }
  }

  if (owner) owner->markAccessed();
  return static_cast<int32_t>(bufsize);
}

int32_t mscWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  std::lock_guard<std::mutex> lock(gIoMutex);
  if (!gAccepting.load()) return -1;
  auto* const dev = gDev.load();
  auto* const owner = gOwner.load();
  if (!buffer || !isRangeValid(dev, lba, offset, bufsize)) {
    if (owner) owner->markIoError();
    return -1;
  }

  const uint64_t start = static_cast<uint64_t>(lba) * gBlockSize + offset;
  auto* input = buffer;
  uint32_t remaining = bufsize;
  uint64_t cursor = start;

  if ((offset % gBlockSize) == 0 && (bufsize % gBlockSize) == 0) {
    const size_t sectors = bufsize / gBlockSize;
    if (!dev->writeSectors(static_cast<Sector_t>(start / gBlockSize), input, sectors)) {
      if (owner) owner->markIoError();
      return -1;
    }
  } else {
    while (remaining > 0) {
      const auto sector = static_cast<Sector_t>(cursor / gBlockSize);
      const size_t within = static_cast<size_t>(cursor % gBlockSize);
      const size_t count = std::min<size_t>(gBlockSize - within, remaining);
      if (!dev->readSector(sector, gSectorScratch)) {
        if (owner) owner->markIoError();
        return -1;
      }
      memcpy(gSectorScratch + within, input, count);
      if (!dev->writeSector(sector, gSectorScratch)) {
        if (owner) owner->markIoError();
        return -1;
      }
      input += count;
      cursor += count;
      remaining -= count;
    }
  }

  if (owner) owner->markAccessed();
  return static_cast<int32_t>(bufsize);
}

bool mscStartStop(uint8_t /*power_condition*/, bool start, bool load_eject) {
  std::lock_guard<std::mutex> lock(gIoMutex);
  if (!gAccepting.load()) return false;
  if (load_eject && !start) {
    if (auto* const dev = gDev.load(); dev && !dev->syncDevice()) {
      if (auto* const owner = gOwner.load()) owner->markIoError();
      return false;
    }
    gAccepting.store(false);
    if (auto* const owner = gOwner.load()) owner->markEjected();
  }
  return true;
}

}  // namespace

bool UsbMassStorage::begin(FsBlockDeviceInterface* dev, uint16_t sectorSize) {
  std::lock_guard<std::mutex> lock(gIoMutex);
  if (_active || gOwner.load() || !dev || dev->sectorCount() == 0 ||
      (sectorSize != 512 && sectorSize != 4096)) return false;
  gSectorScratch=static_cast<uint8_t*>(heap_caps_malloc(sectorSize,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(!gSectorScratch)return false;
  gBlockSize = sectorSize;
  if (!gEventsRegistered) {
    USB.onEvent(ARDUINO_USB_STARTED_EVENT,mountedEvent);
    gEventsRegistered=true;
  }

  gDev.store(dev);
  gOwner.store(this);
  _state.store(UsbMassStorageState::WaitingForHost);
  _hostSeen.store(false);
  gMsc.vendorID("FreeInk");
  gMsc.productID("PicoInk Storage");
  gMsc.productRevision("1.0");
  gMsc.mediaPresent(true);
  gMsc.isWritable(true);
  gMsc.onStartStop(mscStartStop);
  gMsc.onRead(mscRead);
  gMsc.onWrite(mscWrite);
  if (!gMsc.begin(dev->sectorCount(), gBlockSize)) {
    heap_caps_free(gSectorScratch);gSectorScratch=nullptr;
    gMsc.mediaPresent(false);
    gOwner.store(nullptr);
    gDev.store(nullptr);
    _state.store(UsbMassStorageState::Idle);
    return false;
  }
  if (!USB.begin()) {
    heap_caps_free(gSectorScratch);gSectorScratch=nullptr;
    gMsc.mediaPresent(false);
    gOwner.store(nullptr);
    gDev.store(nullptr);
    _state.store(UsbMassStorageState::Idle);
    return false;
  }
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
  tud_disconnect();
#endif
  routeStorage(true);
  _active = true;
  gAccepting.store(true);
  tud_connect();  // Reconnect when re-entering MSC after a prior session.
  return true;
}

void UsbMassStorage::end() {
  if (!_active) return;
  // Block new callbacks first, then wait for the actual in-flight block I/O.
  // tud_mounted()==false alone is NOT an acknowledgement from a callback.
  gAccepting.store(false);
  tud_disconnect();
  std::lock_guard<std::mutex> lock(gIoMutex);
  if (auto* const dev = gDev.load()) dev->syncDevice();
  gMsc.mediaPresent(false);
  _active = false;
  gOwner.store(nullptr);
  gDev.store(nullptr);
  heap_caps_free(gSectorScratch);gSectorScratch=nullptr;
  // Keep the statically registered LUN and function pointers alive. Arduino's
  // end() clears them without synchronizing with TinyUSB's SCSI dispatcher.
  _state.store(UsbMassStorageState::Idle);
  _hostSeen.store(false);
  routeStorage(false); // Restore debug only after in-flight storage I/O drains.
}

UsbMassStorageState UsbMassStorage::state() const {
  if (!_active) return UsbMassStorageState::Idle;
  const auto current = _state.load();
  if (current == UsbMassStorageState::Ejected) return current;

  if (!tud_mounted()) {
    return _hostSeen.load() ? UsbMassStorageState::Disconnected : UsbMassStorageState::WaitingForHost;
  }

  // Keep the error visible while the host is mounted, but report a later cable
  // removal so the application can safely reclaim its raw storage session.
  if (current == UsbMassStorageState::IoError) return current;

  // A soft disconnect does not immediately clear TinyUSB's old configured
  // flag. Only a fresh mount callback or real I/O establishes this session.
  if (!_hostSeen.load()) return UsbMassStorageState::WaitingForHost;
  auto expected = UsbMassStorageState::WaitingForHost;
  _state.compare_exchange_strong(expected, UsbMassStorageState::Connected);
  return _state.load();
}

bool UsbMassStorage::hostConnected() const {
  const auto current = state();
  return current == UsbMassStorageState::Connected || current == UsbMassStorageState::Accessed;
}

bool UsbMassStorage::disconnectHost() const { return _active && tud_disconnect(); }

bool UsbMassStorage::hostSuspended() const { return _active && tud_mounted() && tud_suspended(); }

void UsbMassStorage::markConnected() const {
  _hostSeen.store(true);
  auto expected=UsbMassStorageState::WaitingForHost;
  _state.compare_exchange_strong(expected,UsbMassStorageState::Connected);
}

void UsbMassStorage::markAccessed() const {
  _hostSeen.store(true);
  auto current = _state.load();
  while (current != UsbMassStorageState::Ejected && current != UsbMassStorageState::IoError) {
    if (_state.compare_exchange_weak(current, UsbMassStorageState::Accessed)) return;
  }
}

void UsbMassStorage::markEjected() const { _state.store(UsbMassStorageState::Ejected); }

void UsbMassStorage::markIoError() const {
  _hostSeen.store(true);
  _state.store(UsbMassStorageState::IoError);
}

}  // namespace freeink

#endif  // FREEINK_CAP_USB_MSC
