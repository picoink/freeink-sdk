#include "PageTurner.h"
#include <BoardConfig.h>
#if FREEINK_CAP_BLE_HID_HOST && !defined(FREEINK_HOST)
#include <Preferences.h>
#include <algorithm>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
namespace freeink {
namespace turner {
using Command = PageTurner::Command;

using Snapshot = PageTurner::Snapshot;
struct Config {
  uint8_t enabled = 0;
  char selected[18]{};
};
std::mutex mutex;
TaskHandle_t task = nullptr;
PageTurner::Request requests[8]{};
PageTurner::Event events[16]{};
uint8_t requestHead = 0, requestTail = 0, eventHead = 0, eventTail = 0;
Snapshot published{}, current{};
Config config{};
bool suspended = false, initialized = false;
uint32_t generation = 0, stoppedGeneration = UINT32_MAX;
uint32_t switchAt = 0, passkeySince = 0, retryAt = 0;
char nextPeer[18]{}, connectingPeer[18]{}, forgetting[18]{};
bool armSelected = false, scanRequested = false;
auto &host = BleKeyboardHost::getInstance();
void message(const char *value) {
  snprintf(current.message, sizeof(current.message), "%s", value);
}
bool save(const Config &value) {
  Preferences p;
  if (!p.begin("freeink-turner", false)) {
    message("无法保存翻页器设置");
    return false;
  }
  const bool ok = p.putBytes("link", &value, sizeof(value)) == sizeof(value);
  p.end();
  if (ok)
    config = value;
  else
    message("无法保存翻页器设置");
  return ok;
}
void load() {
  Preferences p;
  Config value{};
  if (p.begin("freeink-turner", true)) {
    // The connection preferences are the prefix of the original NVS record.
    uint8_t record[192];
    size_t size = p.getBytes("link", record, sizeof(record));
    if (!size)
      size = p.getBytes("config", record, sizeof(record));
    if (size >= sizeof(value) && size <= sizeof(record)) {
      memcpy(&value, record, sizeof(value));
      value.selected[17] = 0;
      if (value.enabled <= 1)
        config = value;
    }
    p.end();
  }
}
bool paired(const char *address) {
  for (unsigned i = 0; i < host.pairedCount(); ++i)
    if (!strcmp(host.paired(i).addr, address))
      return true;
  return false;
}
void processInput() {
  RawButtonEvent edge{};
  while (host.popRawButton(edge)) {
    if (!host.isConnected() || strcmp(host.connectedAddr(), config.selected))
      continue;
    std::lock_guard<std::mutex> lock(mutex);
    const auto next = uint8_t((eventHead + 1) % 16);
    if (suspended || next == eventTail)
      continue;
    auto &event = events[eventHead];
    event.raw = edge;
    snprintf(event.address, sizeof(event.address), "%s", host.connectedAddr());
    eventHead = next;
  }
  KeyEvent ignored{};
  while (host.popKey(ignored)) {
  }
}
void execute(const PageTurner::Request &r) {
  current.message[0] = 0;
  if (r.command == Command::Enable) {
    Config value = config;
    value.enabled = r.value ? 1 : 0;
    if (save(value)) {
      scanRequested = false;
      nextPeer[0] = connectingPeer[0] = 0;
      retryAt = 0;
      armSelected = value.enabled;
    }
    return;
  }
  if (!config.enabled || !host.isRunning() || host.isStopping()) {
    message("请先打开翻页器");
    return;
  }
  switch (r.command) {
  case Command::Enable:
    break;
  case Command::Scan:
    nextPeer[0] = connectingPeer[0] = 0;
    host.disconnect();
    armSelected = false;
    scanRequested = true;
    switchAt = millis();
    break;
  case Command::Connect: {
    scanRequested = false;
    if (!paired(r.address) &&
        host.pairedCount() >= BleKeyboardHost::kMaxBonds) {
      message("配对列表已满，请先移除不用的设备");
      break;
    }
    if (host.isConnected() && !strcmp(host.connectedAddr(), r.address))
      break;
    armSelected = false;
    connectingPeer[0] = 0;
    switchAt = millis();
    snprintf(nextPeer, sizeof(nextPeer), "%s", r.address);
    host.disconnect();
    break;
  }
  case Command::Disconnect:
    scanRequested = false;
    nextPeer[0] = connectingPeer[0] = 0;
    armSelected = false;
    host.disconnect();
    break;
  case Command::Reconnect:
    scanRequested = false;
    nextPeer[0] = connectingPeer[0] = 0;
    host.stopScan();
    if (!host.isConnected() || strcmp(host.connectedAddr(), config.selected))
      host.disconnect();
    armSelected =
        !host.isConnected() || strcmp(host.connectedAddr(), config.selected);
    if (!config.selected[0]) {
      armSelected = false;
      message("请先选择一个翻页器");
    }
    break;
  case Command::Forget:
    scanRequested = false;
    nextPeer[0] = connectingPeer[0] = 0;
    host.forget(r.address);
    snprintf(forgetting, sizeof(forgetting), "%s", r.address);
    armSelected = false;
    break;
  }
}
void step() {
  if (!initialized) {
    load();
    initialized = true;
    armSelected = config.enabled;
  }
  bool sleep;
  uint32_t sequence;
  {
    std::lock_guard<std::mutex> lock(mutex);
    sleep = suspended;
    sequence = generation;
  }
  if (sleep) {
    scanRequested = false;
    nextPeer[0] = connectingPeer[0] = 0;
    host.stopScan();
    host.end(0);
    RawButtonEvent raw;
    KeyEvent key;
    while (host.popRawButton(raw)) {
    }
    while (host.popKey(key)) {
    }
    std::lock_guard<std::mutex> lock(mutex);
    requestTail = requestHead;
    eventTail = eventHead;
    if (!host.isRunning() && !host.isStopping())
      stoppedGeneration = sequence;
  } else {
    PageTurner::Request r{};
    for (unsigned n = 0; n < 8; ++n) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (requestTail == requestHead)
          break;
        r = requests[requestTail];
        requestTail = uint8_t((requestTail + 1) % 8);
      }
      execute(r);
    }
    if (!config.enabled || host.isStopping()) {
      scanRequested = false;
      nextPeer[0] = connectingPeer[0] = 0;
      host.end(0);
    } else if (!host.isRunning() && int32_t(millis() - retryAt) >= 0) {
      retryAt = millis() + 4000;
      if (host.begin("PicoInk")) {
        host.disconnect();
        armSelected = true;
      } else
        message("无法启动翻页器，请重试");
    }
    if (config.enabled && host.isRunning() && !host.isStopping()) {
      host.poll();
      if (armSelected && config.selected[0] && !host.isConnected() &&
          !host.isConnecting() && !host.isScanning()) {
        armSelected = false;
        if (!host.armSelectedPeerReconnect(config.selected))
          message("无法重连，请重新选择设备");
      }
      if (scanRequested) {
        if (!host.isConnected() && !host.isConnecting())
          host.startScan(15000);
        if (host.isScanning())
          scanRequested = false;
        else if (millis() - switchAt >= 5000) {
          scanRequested = false;
          armSelected = true;
          message("搜索失败，请重试");
        }
      }
      if (nextPeer[0]) {
        if (!host.isConnected() && !host.isConnecting() &&
            host.connectOnly(nextPeer)) {
          memcpy(connectingPeer, nextPeer, sizeof(nextPeer));
          nextPeer[0] = 0;
        } else if (millis() - switchAt >= 5000) {
          nextPeer[0] = 0;
          armSelected = true;
          message("暂时无法连接，请重试");
        }
      }
      char error[48];
      if (host.takeConnectFailure(error, sizeof(error))) {
        if (connectingPeer[0]) {
          host.disconnect();
          connectingPeer[0] = 0;
          armSelected = true;
        }
        message("连接失败，请检查翻页器后重试");
      }
      if (connectingPeer[0] && host.isConnected() &&
          !strcmp(host.connectedAddr(), connectingPeer)) {
        Config value = config;
        memcpy(value.selected, connectingPeer, sizeof(value.selected));
        if (!save(value)) {
          host.disconnect();
          armSelected = true;
        }
        connectingPeer[0] = 0;
      }
      processInput();
      uint32_t passkey;
      if (host.takePairingPasskey(passkey)) {
        current.hasPasskey = true;
        current.passkey = passkey;
        passkeySince = millis();
      }
      if (current.hasPasskey &&
          (host.isConnected() || millis() - passkeySince >= 30000))
        current.hasPasskey = false;
      if (forgetting[0]) {
        bool found = false;
        for (uint8_t i = 0; i < host.pairedCount(); ++i)
          found |= !strcmp(host.paired(i).addr, forgetting);
        if (!found) {
          Config value = config;
          if (!strcmp(value.selected, forgetting))
            value.selected[0] = 0;
          if (save(value))
            message("已移除设备");
          forgetting[0] = 0;
        }
      }
    }
  }
  current.supported = true;
  current.enabled = config.enabled;
  current.running = host.isRunning();
  current.stopping = host.isStopping();
  current.connected = host.isConnected();
  current.connecting = host.isConnecting() || nextPeer[0] || connectingPeer[0];
  current.scanning = host.isScanning() || scanRequested;
  snprintf(current.selected, sizeof(current.selected), "%s", config.selected);
  snprintf(current.connectedAddress, sizeof(current.connectedAddress), "%s",
           host.connectedAddr());
  snprintf(current.connectedName, sizeof(current.connectedName), "%s",
           host.connectedName());
  current.deviceCount = host.deviceCount();
  current.pairedCount = host.pairedCount();
  for (uint8_t i = 0; i < current.deviceCount; ++i)
    current.devices[i] = host.device(i);
  for (uint8_t i = 0; i < current.pairedCount; ++i)
    current.paired[i] = host.paired(i);
  {
    std::lock_guard<std::mutex> lock(mutex);
    published = current;
  }
}
void worker(void *) {
  for (;;) {
    step();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
} // namespace turner
bool PageTurner::supported() { return true; }
bool PageTurner::begin() {
  std::lock_guard<std::mutex> lock(turner::mutex);
  if (turner::task)
    return true;
  return xTaskCreate(turner::worker, "page-turner", 6144, nullptr, 2,
                     &turner::task) == pdPASS;
}
bool PageTurner::request(const Request &r) {
  if (!begin() || r.command > Command::Reconnect ||
      (r.command == Command::Enable && r.value > 1) ||
      !memchr(r.address, 0, sizeof(r.address)))
    return false;
  std::lock_guard<std::mutex> lock(turner::mutex);
  if (turner::suspended)
    return false;
  const auto next = uint8_t((turner::requestHead + 1) % 8);
  if (next == turner::requestTail)
    return false;
  auto &queued = turner::requests[turner::requestHead];
  queued = r;
  for (char &c : queued.address) {
    if (c >= 'A' && c <= 'F')
      c += 'a' - 'A';
  }
  turner::requestHead = next;
  return true;
}
PageTurner::Snapshot PageTurner::snapshot() {
  std::lock_guard<std::mutex> lock(turner::mutex);
  auto s = turner::published;
  s.supported = true;
  return s;
}
bool PageTurner::pop(Event &e) {
  std::lock_guard<std::mutex> lock(turner::mutex);
  while (turner::eventTail != turner::eventHead) {
    const auto &event = turner::events[turner::eventTail];
    turner::eventTail = uint8_t((turner::eventTail + 1) % 16);
    if (!turner::suspended && turner::published.connected &&
        !strcmp(event.address, turner::published.connectedAddress)) {
      e = event;
      return true;
    }
  }
  return false;
}
bool PageTurner::suspend() {
  std::lock_guard<std::mutex> lock(turner::mutex);
  if (!turner::task)
    return true;
  if (!turner::suspended) {
    turner::suspended = true;
    ++turner::generation;
  }
  return turner::stoppedGeneration == turner::generation;
}
void PageTurner::resume() {
  std::lock_guard<std::mutex> lock(turner::mutex);
  if (turner::suspended) {
    turner::suspended = false;
    ++turner::generation;
  }
}
} // namespace freeink
#else
namespace freeink {
bool PageTurner::supported() { return false; }
bool PageTurner::begin() { return false; }
bool PageTurner::request(const Request &) { return false; }
PageTurner::Snapshot PageTurner::snapshot() { return {}; }
bool PageTurner::pop(Event &) { return false; }
bool PageTurner::suspend() { return true; }
void PageTurner::resume() {}
} // namespace freeink
#endif
namespace freeink {
PageTurner &PageTurner::instance() {
  static PageTurner service;
  return service;
}
} // namespace freeink
