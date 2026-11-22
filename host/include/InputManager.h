#pragma once
#include "Arduino.h"
#include <deque>
#include <mutex>
#include <array>
class InputManager {
 public:
  static constexpr uint8_t BTN_BACK=0,BTN_CONFIRM=1,BTN_LEFT=2,BTN_RIGHT=3,BTN_UP=4,BTN_DOWN=5,BTN_POWER=6;
  void begin() {}
  bool beginAsync(unsigned=2,unsigned=10) { running_=true;return true; }
  bool endAsync() { running_=false;return true; }
  bool asyncRunning() const { return running_; }
  bool prepareForDeepSleep() { return true; }
  void update() {}
  bool isPressed(uint8_t key) const;
  bool isTouchPressed() const { return touchHeld_; }
  bool isPowerButtonPhysicallyPressed() const { return isPressed(BTN_POWER); }
  unsigned long getPowerButtonHeldTime() const;
  bool popPress(uint8_t& key);
  bool popHomeKey(bool& held);
  bool popTouchTap(float& x,float& y);
  bool popSwipe(float& x,float& y,float& x1,float& y1);
  void hostKey(uint8_t key,bool down);
  void hostHome(bool held);
  void hostTouch(bool held) { touchHeld_=held; }
  void hostTap(float x,float y);
  void hostSwipe(float x,float y,float x1,float y1);
 private:
  mutable std::mutex mutex_;
  std::deque<uint8_t> keys_;
  std::deque<bool> homes_;
  std::deque<std::array<float,2>> taps_;
  std::deque<std::array<float,4>> swipes_;
  bool pressed_[7]{},running_=false;
  std::atomic<bool> touchHeld_{false};
  unsigned long powerAt_=0;
};
