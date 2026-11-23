#include "HostMemory.h"
#include "InputManager.h"

using namespace freeink::host;
namespace {
constexpr uint16_t bit(InputManager::HostKey key) { return 1u << unsigned(key); }
constexpr unsigned sourceCount = unsigned(InputManager::HostSource::Count);
}  // namespace

bool InputManager::beginAsync(unsigned, unsigned, uint8_t capacity, bool ordered) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) return true;
  ordered_ = ordered; eventCapacity_ = capacity;
  events_.clear(); eventOverflow_ = eventSequence_ = 0;
  running_ = true;
  return true;
}
bool InputManager::endAsync() {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = homeArmed_ = false;
  events_.clear(); eventOverflow_ = 0;
  keys_.clear();
  edges_.clear(); edgeOverflow_ = false;
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
      millis() - homeAt_ >= homeLongPressMs_) {
    if (homes_.size() < 16) homes_.push_back(true);
    freeink::OrderedInput e{}; e.kind = freeink::OrderedInput::Home; e.pressed = true; publishEventLocked(e);
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
    if (down != pressed_[key] && running_) {
      if (edges_.size() < 32) edges_.push_back({uint32_t(millis()), uint8_t(key), down});
      else edgeOverflow_ = true;
      freeink::OrderedInput e{}; e.kind = freeink::OrderedInput::Key; e.button = key; e.pressed = down; publishEventLocked(e);
    }
    if (down && !pressed_[key]) {
      if (running_ && keys_.size() < 32) keys_.push_back(key);
      if (key == BTN_POWER) powerAt_ = millis();
    }
    pressed_[key] = down;
  }
}
bool InputManager::popKeyEvent(KeyEvent& event) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (edgeOverflow_) {
    edges_.clear(); edgeOverflow_ = false;
    event = {uint32_t(millis()), 255, false}; return true;
  }
  if (edges_.empty()) return false;
  event = edges_.front(); edges_.pop_front(); return true;
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
    if (running_ && homeArmed_ && !homeLongFired_) {
      if (homes_.size() < 16) homes_.push_back(false);
      freeink::OrderedInput e{}; e.kind = freeink::OrderedInput::Home; publishEventLocked(e);
    }
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
  if (running_) {
    if (homes_.size() < 16) homes_.push_back(held);
    freeink::OrderedInput e{}; e.kind = freeink::OrderedInput::Home; e.pressed = held; publishEventLocked(e);
  }
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
  if (running_) {
    if (taps_.size() < 32) taps_.push_back({x, y});
    freeink::OrderedInput e{}; e.kind = freeink::OrderedInput::Tap; e.x = x; e.y = y; publishEventLocked(e);
  }
}
void InputManager::hostSwipe(float x, float y, float x1, float y1) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) {
    if (swipes_.size() < 16) swipes_.push_back({x, y, x1, y1});
    freeink::OrderedInput e{}; e.kind = freeink::OrderedInput::Swipe; e.x = x; e.y = y; e.x1 = x1; e.y1 = y1; publishEventLocked(e);
  }
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

void InputManager::publishEventLocked(freeink::OrderedInput event) {
  if (!ordered_) return;
  event.timestamp = millis(); event.sequence = ++eventSequence_;
  if (events_.size() < eventCapacity_) events_.push_back(event);
  else ++eventOverflow_;
}
bool InputManager::popEvent(freeink::OrderedInput& event) {
  HostScope host;
  std::lock_guard<std::mutex> lock(mutex_);
  pollHomeLocked();
  if (eventOverflow_) {
    event = {}; event.kind = freeink::OrderedInput::Reset;
    event.timestamp = millis(); event.dropped = eventOverflow_ + events_.size();
    events_.clear(); eventOverflow_ = 0; return true;
  }
  if (events_.empty()) return false;
  event = events_.front(); events_.pop_front(); return true;
}
