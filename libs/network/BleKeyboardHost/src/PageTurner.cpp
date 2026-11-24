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
using Action = PageTurner::Action;
using Snapshot = PageTurner::Snapshot;
struct Config {
  uint8_t enabled = 0;
  char selected[18]{};
  PageTurner::Binding bindings[BleKeyboardHost::kMaxBonds]{};
};
std::mutex mutex;
TaskHandle_t task = nullptr;
PageTurner::Request requests[8]{};
PageTurner::Event events[16]{};
uint8_t requestHead = 0, requestTail = 0, eventHead = 0, eventTail = 0;
Snapshot published{}, current{};
Config config{};
bool suspended = false, initialized = false, cancelRequested = false;
uint32_t generation = 0, stoppedGeneration = UINT32_MAX;
uint8_t learning = 0;
uint32_t learningSince = 0, candidate = 0, passkeySince = 0, retryAt = 0;
char learningPeer[18]{}, forgetting[18]{};
bool armSelected = false;
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
  const bool ok = p.putBytes("config", &value, sizeof(value)) == sizeof(value);
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
    if (p.getBytes("config", &value, sizeof(value)) == sizeof(value) &&
        value.enabled <= 1) {
      value.selected[17] = 0;
      for (auto &b : value.bindings)
        b.address[17] = 0;
      config = value;
    }
    p.end();
  }
}
PageTurner::Binding *binding(Config &value, const char *address,
                             bool create = false) {
  if (!address || !address[0])
    return nullptr;
  for (auto &b : value.bindings)
    if (!strcmp(b.address, address))
      return &b;
  if (create)
    for (auto &b : value.bindings)
      if (!b.address[0]) {
        snprintf(b.address, sizeof(b.address), "%s", address);
        return &b;
      }
  return nullptr;
}
void cancelLearning() {
  learning = 0;
  candidate = 0;
  learningPeer[0] = 0;
}
void send(Action action, uint32_t time) {
  if (action == Action::None)
    return;
  std::lock_guard<std::mutex> lock(mutex);
  if (suspended)
    return;
  const auto next = uint8_t((eventHead + 1) % 16);
  if (next != eventTail) {
    events[eventHead] = {action, time};
    eventHead = next;
  }
}
void processInput() {
  RawButtonEvent edge{};
  while (host.popRawButton(edge)) {
    const char *peer = host.connectedAddr();
    if (!host.isConnected())
      continue;
    if (learning) {
      if (strcmp(peer, learningPeer)) {
        cancelLearning();
        message("设备已切换，请重新学习按键");
        continue;
      }
      if (edge.pressed) {
        if (!candidate)
          candidate = edge.code();
        continue;
      }
      if (!candidate || edge.code() != candidate)
        continue;
      if (edge.wasRest) {
        candidate = 0;
        continue;
      }
      Config value = config;
      auto *b = binding(value, peer, true);
      if (!b) {
        message("按键记录已满，请先移除不用的设备");
        cancelLearning();
        continue;
      }
      // One physical button has one action on a given peripheral.
      for (auto &code : b->codes)
        if (code == candidate)
          code = 0;
      b->codes[learning - 1] = candidate;
      if (save(value))
        message("按键已保存");
      cancelLearning();
      continue;
    }
    if (!edge.pressed)
      continue;
    Action action = Action::None;
    if (auto *b = binding(config, peer))
      for (uint8_t i = 0; i < 3; ++i)
        if (b->codes[i] && b->codes[i] == edge.code())
          action = Action(i + 1);
    if (action == Action::None) {
      // Keyboard defaults are only a fallback for buttons with no learned
      // binding.
      if (edge.keycode == 0x50 || edge.keycode == 0x52 || edge.keycode == 0x4B)
        action = Action::Previous;
      if (edge.keycode == 0x4F || edge.keycode == 0x51 ||
          edge.keycode == 0x4E || edge.keycode == 0x2C)
        action = Action::Next;
    }
    send(action, edge.atMs);
  }
  KeyEvent ignored{};
  while (host.popKey(ignored)) {
  } // Raw edges carry keyboard fallback identities too.
}
void execute(const PageTurner::Request &r) {
  current.message[0] = 0;
  if (r.command == Command::Enable) {
    Config value = config;
    value.enabled = r.value ? 1 : 0;
    if (save(value)) {
      cancelLearning();
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
    cancelLearning();
    host.disconnect();
    armSelected = false;
    // A live link closes asynchronously. Scan may be retried after it closes.
    if (host.isConnected() || host.isConnecting()) {
      message("正在断开，请稍后重新搜索");
      break;
    }
    host.startScan(15000);
    if (!host.isScanning())
      message("蓝牙正在准备，请稍后重新搜索");
    break;
  case Command::Connect: {
    cancelLearning();
    armSelected = false;
    if (host.isConnected() || host.isConnecting()) {
      message("请先断开当前设备");
      break;
    }
    if (!host.connect(r.address)) {
      message("暂时无法连接，请重试");
      break;
    }
    Config value = config;
    snprintf(value.selected, sizeof(value.selected), "%s", r.address);
    if (!save(value))
      host.disconnect();
    break;
  }
  case Command::Disconnect:
    cancelLearning();
    armSelected = false;
    host.disconnect();
    break;
  case Command::Reconnect:
    cancelLearning();
    host.stopScan();
    armSelected = true;
    if (!config.selected[0]) {
      armSelected = false;
      message("请先选择一个翻页器");
    }
    break;
  case Command::Forget:
    cancelLearning();
    host.forget(r.address);
    snprintf(forgetting, sizeof(forgetting), "%s", r.address);
    armSelected = false;
    break;
  case Command::Learn:
    if (!host.isConnected() || r.value < 1 || r.value > 3) {
      message("连接后才能学习按键");
      break;
    }
    learning = r.value;
    learningSince = millis();
    candidate = 0;
    snprintf(learningPeer, sizeof(learningPeer), "%s", host.connectedAddr());
    message("请按下并松开要使用的按键");
    break;
  case Command::CancelLearn:
    cancelLearning();
    break;
  case Command::ClearBindings: {
    Config value = config;
    auto *b = binding(value, host.isConnected() ? host.connectedAddr()
                                                : config.selected);
    if (b) {
      for (auto &code : b->codes)
        code = 0;
      if (save(value))
        message("已恢复默认按键");
    }
    break;
  }
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
    if (cancelRequested) {
      cancelLearning();
      cancelRequested = false;
    }
  }
  if (sleep) {
    cancelLearning();
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
      cancelLearning();
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
      if (learning &&
          (!host.isConnected() || millis() - learningSince >= 30000)) {
        cancelLearning();
        message("按键学习已结束，请重试");
      }
      processInput();
      char error[48];
      if (host.takeConnectFailure(error, sizeof(error)))
        message("连接失败，请检查翻页器的配对模式后重试");
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
          if (auto *b = binding(value, forgetting))
            *b = {};
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
  current.connecting = host.isConnecting();
  current.scanning = host.isScanning();
  current.learning = learning;
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
  memcpy(current.bindings, config.bindings, sizeof(current.bindings));
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
  if (!begin() || r.command > Command::ClearBindings || r.value > 3 ||
      !memchr(r.address, 0, sizeof(r.address)))
    return false;
  std::lock_guard<std::mutex> lock(turner::mutex);
  if (turner::suspended)
    return false;
  if (r.command == Command::CancelLearn) {
    turner::cancelRequested = true;
    Request retained[8]{};
    unsigned count = 0;
    while (turner::requestTail != turner::requestHead) {
      auto item = turner::requests[turner::requestTail];
      turner::requestTail = uint8_t((turner::requestTail + 1) % 8);
      if (item.command != Command::Learn)
        retained[count++] = item;
    }
    turner::requestTail = 0;
    turner::requestHead = count;
    for (unsigned i = 0; i < count; ++i)
      turner::requests[i] = retained[i];
    return true;
  }
  const auto next = uint8_t((turner::requestHead + 1) % 8);
  if (next == turner::requestTail)
    return false;
  turner::requests[turner::requestHead] = r;
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
  if (turner::eventTail == turner::eventHead)
    return false;
  e = turner::events[turner::eventTail];
  turner::eventTail = uint8_t((turner::eventTail + 1) % 16);
  return true;
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
