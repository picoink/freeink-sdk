#pragma once
#include <cstddef>
#include <cstdint>

// Capability queries never initialize the radio or touch preferences.
#ifndef FREEINK_CAP_BLUETOOTH_PAN
#define FREEINK_CAP_BLUETOOTH_PAN 0
#endif
#if !defined(FREEINK_HOST) && defined(ARDUINO_ARCH_ESP32)
#include <sdkconfig.h>
#include <soc/soc_caps.h>
#endif

namespace freeink {
class BluetoothPan {
 public:
  enum class Result : uint8_t {
    Ok,
    Unsupported,
    Busy,
    InvalidArgument,
    Failed
  };
  enum class State : uint8_t {
    Off,
    Starting,
    Idle,
    Scanning,
    Connecting,
    Pairing,
    ObtainingIp,
    Connected,
    RetryWait,
    Stopping,
    Error
  };
  struct Device {
    uint8_t address[6]{};
    char name[64]{};
    int8_t rssi = 0;
  };
  static constexpr size_t MaxDevices = 16;
  struct Status {
    State state = State::Off;
    bool enabled = false, pairing = false, saved = false;
    uint32_t pairingCode = 0, revision = 0, connection = 0, droppedFrames = 0;
    Device peer{};
    char ip[16]{};
    char message[96] = "Bluetooth network off";
    uint8_t count = 0;
    Device devices[MaxDevices]{};
  };
  static constexpr bool supported() {
#if !defined(FREEINK_HOST) && FREEINK_CAP_BLUETOOTH_PAN &&                 \
    defined(SOC_BT_CLASSIC_SUPPORTED) && SOC_BT_CLASSIC_SUPPORTED &&       \
    defined(CONFIG_BT_BLUEDROID_ENABLED) && CONFIG_BT_BLUEDROID_ENABLED && \
    defined(CONFIG_BT_CLASSIC_ENABLED) && CONFIG_BT_CLASSIC_ENABLED &&     \
    defined(CONFIG_BT_PAN_ENABLED) && CONFIG_BT_PAN_ENABLED
    return true;
#else
    return false;
#endif
  }
  static BluetoothPan& instance();
  // Asynchronous commands: Ok means accepted. Poll status for completion.
  Result begin();
  Result end();
  Result scan();
  Result connect(const uint8_t address[6]);
  Result disconnect();  // Stops the radio; enable again to reconnect.
  Result confirmPairing(bool accept);
  Result forget();
  Status status() const;
  static const char* stateName(State);

 private:
  BluetoothPan() = default;
};
}  // namespace freeink
