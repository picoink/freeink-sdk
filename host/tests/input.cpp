#include <InputManager.h>

#include <cassert>
#include <iostream>

int main() {
  using K = InputManager::HostKey;
  using S = InputManager::HostSource;
  using namespace freeink::host;
  InputManager input;
  input.beginAsync();
  uint8_t key;
  InputManager::KeyEvent edge;
  bool held;
  for (const auto device : {K::TouchPrevious, K::TouchNext, K::Boot, K::VolumeUp, K::VolumeDown, K::Power}) {
    const auto logical = device == K::Boot                                       ? InputManager::BTN_CONFIRM
                         : device == K::Power                                    ? InputManager::BTN_POWER
                         : (device == K::TouchPrevious || device == K::VolumeUp) ? InputManager::BTN_UP
                                                                                 : InputManager::BTN_DOWN;
    input.hostDeviceKey(device, true, S::Desktop);
    input.hostDeviceKey(device, true, S::Desktop);
    assert(input.hostHeld(device) && input.isPressed(logical));
    assert(input.popPress(key) && key == logical && !input.popPress(key));
    assert(input.popKeyEvent(edge) && edge.button == logical && edge.pressed);
    const uint32_t pressedAt = edge.timestamp;
    runtime().timeOffset += 25;
    input.hostDeviceKey(device, false, S::Desktop);
    assert(!input.isPressed(logical));
    assert(input.popKeyEvent(edge) && edge.button == logical && !edge.pressed);
    assert(uint32_t(edge.timestamp - pressedAt) >= 25 && !input.popKeyEvent(edge));
  }
  // Both physical devices and independent clients contribute to the same level.
  input.hostDeviceKey(K::VolumeUp, true, S::Desktop);
  input.hostDeviceKey(K::TouchPrevious, true, S::Desktop);
  input.hostDeviceKey(K::VolumeUp, true, S::Protocol);
  assert(input.popPress(key) && !input.popPress(key));
  input.hostDeviceKey(K::TouchPrevious, false, S::Desktop);
  input.hostCancel(S::Desktop);
  assert(input.isPressed(InputManager::BTN_UP));
  input.hostDeviceKey(K::VolumeUp, false, S::Protocol);
  assert(!input.isPressed(InputManager::BTN_UP));
  input.hostDeviceKey(K::TouchHome, true, S::Desktop);
  assert(!input.popHomeKey(held));
  input.hostDeviceKey(K::TouchHome, false, S::Desktop);
  assert(input.popHomeKey(held) && !held && !input.popHomeKey(held));
  input.hostDeviceKey(K::TouchHome, true, S::Desktop);
  runtime().timeOffset += InputManager::HOME_KEY_LONG_PRESS_MS;
  assert(input.popHomeKey(held) && held && !input.popHomeKey(held));
  input.hostDeviceKey(K::TouchHome, false, S::Desktop);
  assert(!input.popHomeKey(held));
  input.hostDeviceKey(K::TouchHome, true, S::Desktop);
  input.hostCancel(S::Desktop);
  runtime().timeOffset += 1000;
  assert(!input.popHomeKey(held));
  input.hostDeviceKey(K::TouchHome, true, S::Desktop);
  input.endAsync();
  input.hostDeviceKey(K::TouchHome, false, S::Desktop);
  input.beginAsync();
  assert(!input.popHomeKey(held));
  assert(!input.popKeyEvent(edge));
  // Overflow cancels an ambiguous gesture; it must never become a double click.
  for (int i = 0; i < 20; ++i) {
    input.hostDeviceKey(K::Boot, true, S::Desktop);
    input.hostDeviceKey(K::Boot, false, S::Desktop);
  }
  assert(input.popKeyEvent(edge) && edge.button == 255 && !input.popKeyEvent(edge));
  input.hostDeviceKey(K::Boot, true, S::Desktop);
  assert(input.popKeyEvent(edge) && edge.button == InputManager::BTN_CONFIRM && edge.pressed);
  input.hostDeviceKey(K::Boot, false, S::Desktop);
  input.hostPowerLevel(S::Desktop, true);
  input.hostPowerLevel(S::Protocol, true);
  input.hostCancel(S::Desktop);
  assert(input.isPowerButtonPhysicallyPressed());
  input.endAsync();
  input.hostPowerLevel(S::Protocol, false);
  assert(!input.isPowerButtonPhysicallyPressed());
  std::cout << "Seven host keys, source merging, HOME holds, cancellation and raw power release passed\n";
}
