#pragma once
inline bool usbBeginOk = true;
using esp_event_base_t=const char*;
inline constexpr int ARDUINO_USB_STARTED_EVENT=0;
inline void (*mountEvent)(void*,esp_event_base_t,int32_t,void*)=nullptr;
struct UsbStub {
  bool begin() { return usbBeginOk; }
  void onEvent(int,decltype(mountEvent) callback) { mountEvent=callback; }
};
inline UsbStub USB;
