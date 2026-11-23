#pragma once
#include <OrderedInput.h>
#include <array>
#include <deque>
#include <mutex>

#include "Arduino.h"
class InputManager {
 public:
  static constexpr uint8_t BTN_BACK = 0, BTN_CONFIRM = 1, BTN_LEFT = 2, BTN_RIGHT = 3, BTN_UP = 4,
                           BTN_DOWN = 5, BTN_POWER = 6;
  enum class HostKey : uint8_t {
    TouchHome,
    TouchPrevious,
    TouchNext,
    Boot,
    VolumeUp,
    VolumeDown,
    Power,
    Count
  };
  enum class HostSource : uint8_t { Desktop, Protocol, Count };
  static constexpr unsigned long HOME_KEY_LONG_PRESS_MS = 700;
  void begin() {}
  bool beginAsync(unsigned = 2, unsigned = 10, uint8_t capacity = 32, bool ordered = false);
  void setHomeLongPressMs(uint32_t ms) { if (!running_) homeLongPressMs_ = ms ? ms : 1; }
  bool popEvent(freeink::OrderedInput&);
  bool endAsync();
  bool asyncRunning() const;
  bool prepareForDeepSleep() { return true; }
  void update() {}
  bool isPressed(uint8_t key) const;
  bool isTouchPressed() const { return touchHeld_; }
  bool isPowerButtonPhysicallyPressed() const { return freeink::host::runtime().powerPressed; }
  unsigned long getPowerButtonHeldTime() const;
  bool popPress(uint8_t& key);
  struct KeyEvent { uint32_t timestamp; uint8_t button; bool pressed; };
  bool popKeyEvent(KeyEvent&);
  bool popHomeKey(bool& held);
  bool popTouchTap(float& x, float& y);
  bool popSwipe(float& x, float& y, float& x1, float& y1);
  // Thread-safe physical inputs. Distinct devices/sources retain their own levels.
  void hostDeviceKey(HostKey key, bool down, HostSource source);
  void hostCancel(HostSource source);
  bool hostHeld(HostKey key) const;
  // Physical POWER is sampled independently of the firmware command queue.
  void hostPowerLevel(HostSource source, bool down);
  void hostKey(uint8_t key, bool down);
  void hostHome(bool held);
  void hostTouch(bool held) { touchHeld_ = held; }
  void hostTap(float x, float y);
  void hostSwipe(float x, float y, float x1, float y1);

 private:
  mutable std::mutex mutex_;
  std::deque<freeink::OrderedInput> events_;
  void publishEventLocked(freeink::OrderedInput);
  bool ordered_ = false;
  uint32_t eventSequence_ = 0, eventOverflow_ = 0, homeLongPressMs_ = 700;
  uint8_t eventCapacity_ = 32;
  std::deque<uint8_t> keys_;
  std::deque<KeyEvent> edges_;
  bool edgeOverflow_ = false;
  std::deque<bool> homes_;
  std::deque<std::array<float, 2>> taps_;
  std::deque<std::array<float, 4>> swipes_;
  void updateButtonsLocked();
  void pollHomeLocked();
  bool homeHeldLocked() const;
  uint16_t sources_[static_cast<unsigned>(HostSource::Count)]{};
  uint8_t legacy_ = 0, powerSources_ = 0;
  bool pressed_[7]{}, running_ = false, homeArmed_ = false, homeLongFired_ = false;
  unsigned long homeAt_ = 0;
  std::atomic<bool> touchHeld_{false};
  unsigned long powerAt_ = 0;
};
