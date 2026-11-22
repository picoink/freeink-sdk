#include "HostMemory.h"
#include "InputManager.h"

using namespace freeink::host;
namespace {
constexpr uint16_t bit(InputManager::HostKey key) { return 1u << unsigned(key); }
constexpr unsigned sourceCount = unsigned(InputManager::HostSource::Count);
}  // namespace

bool InputManager::beginAsync(unsigned, unsigned) {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = true;
  return true;
}
bool InputManager::endAsync() {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = homeArmed_ = false;
  keys_.clear();
  homes_.clear();
  taps_.clear();
  swipes_.clear();
  touchHeld_ = false;
  return true;
}
bool InputManager::asyncRunning() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_;
}
bool InputManager::isPressed(uint8_t key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return key < 7 && pressed_[key];
}
unsigned long InputManager::getPowerButtonHeldTime() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pressed_[BTN_POWER] ? millis() - powerAt_ : 0;
}
bool InputManager::homeHeldLocked() const {
  for (auto source : sources_)
    if (source & bit(HostKey::TouchHome)) return true;
  return false;
}
bool InputManager::hostHeld(HostKey key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (key >= HostKey::Count) return false;
  for (auto source : sources_)
    if (source & bit(key)) return true;
  return false;
}
void InputManager::pollHomeLocked() {
  if (running_ && homeArmed_ && !homeLongFired_ && homeHeldLocked() &&
      millis() - homeAt_ >= HOME_KEY_LONG_PRESS_MS) {
    if (homes_.size() < 16) homes_.push_back(true);
    homeLongFired_ = true;
  }
}
void InputManager::updateButtonsLocked() {
  uint16_t held = 0;
  for (auto source : sources_) held |= source;
  uint8_t logical = legacy_;
  if (held & (bit(HostKey::TouchPrevious) | bit(HostKey::VolumeUp))) logical |= 1u << BTN_UP;
  if (held & (bit(HostKey::TouchNext) | bit(HostKey::VolumeDown))) logical |= 1u << BTN_DOWN;
  if (held & bit(HostKey::Boot)) logical |= 1u << BTN_CONFIRM;
  if (held & bit(HostKey::Power)) logical |= 1u << BTN_POWER;
  for (unsigned key = 0; key < 7; ++key) {
    const bool down = logical & (1u << key);
    if (down && !pressed_[key]) {
      if (running_ && keys_.size() < 32) keys_.push_back(key);
      if (key == BTN_POWER) powerAt_ = millis();
    }
    pressed_[key] = down;
  }
}
void InputManager::hostDeviceKey(HostKey key, bool down, HostSource source) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (key >= HostKey::Count || unsigned(source) >= sourceCount) return;
  pollHomeLocked();
  const bool wasHome = homeHeldLocked();
  auto& mask = sources_[unsigned(source)];
  if (down)
    mask |= bit(key);
  else
    mask &= ~bit(key);
  const bool home = homeHeldLocked();
  if (home && !wasHome) {
    homeAt_ = millis();
    homeArmed_ = running_;
    homeLongFired_ = false;
  } else if (wasHome && !home) {
    if (running_ && homeArmed_ && !homeLongFired_ && homes_.size() < 16) homes_.push_back(false);
    homeArmed_ = false;
  }
  updateButtonsLocked();
}
void InputManager::hostPowerLevel(HostSource source, bool down) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (unsigned(source) >= sourceCount) return;
  if (down)
    powerSources_ |= 1u << unsigned(source);
  else
    powerSources_ &= ~(1u << unsigned(source));
  runtime().powerPressed = powerSources_ != 0;
}
void InputManager::hostCancel(HostSource source) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (unsigned(source) >= sourceCount) return;
  sources_[unsigned(source)] = 0;
  powerSources_ &= ~(1u << unsigned(source));
  runtime().powerPressed = powerSources_ != 0;
  // Losing focus cancels a pending HOME gesture; it is not a short release.
  if (!homeHeldLocked()) homeArmed_ = false;
  updateButtonsLocked();
}
void InputManager::hostKey(uint8_t key, bool down) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (key >= 7) return;
  if (down)
    legacy_ |= 1u << key;
  else
    legacy_ &= ~(1u << key);
  updateButtonsLocked();
}
void InputManager::hostHome(bool held) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_ && homes_.size() < 16) homes_.push_back(held);
}
bool InputManager::popHomeKey(bool& held) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  pollHomeLocked();
  if (homes_.empty()) return false;
  held = homes_.front();
  homes_.pop_front();
  return true;
}
bool InputManager::popPress(uint8_t& key) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (keys_.empty()) return false;
  key = keys_.front();
  keys_.pop_front();
  return true;
}
void InputManager::hostTap(float x, float y) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_ && taps_.size() < 32) taps_.push_back({x, y});
}
void InputManager::hostSwipe(float x, float y, float x1, float y1) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_ && swipes_.size() < 16) swipes_.push_back({x, y, x1, y1});
}
bool InputManager::popTouchTap(float& x, float& y) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (taps_.empty()) return false;
  auto point = taps_.front();
  taps_.pop_front();
  x = point[0];
  y = point[1];
  return true;
}
bool InputManager::popSwipe(float& x, float& y, float& x1, float& y1) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (swipes_.empty()) return false;
  auto points = swipes_.front();
  swipes_.pop_front();
  x = points[0];
  y = points[1];
  x1 = points[2];
  y1 = points[3];
  return true;
}
