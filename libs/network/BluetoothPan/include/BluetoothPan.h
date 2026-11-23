#pragma once

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
  static constexpr bool supported() {
#if !defined(FREEINK_HOST) && FREEINK_CAP_BLUETOOTH_PAN && \
    defined(SOC_BT_CLASSIC_SUPPORTED) && SOC_BT_CLASSIC_SUPPORTED && \
    defined(CONFIG_BT_BLUEDROID_ENABLED) && CONFIG_BT_BLUEDROID_ENABLED && \
    defined(CONFIG_BT_CLASSIC_ENABLED) && CONFIG_BT_CLASSIC_ENABLED && \
    defined(CONFIG_BT_PAN_ENABLED) && CONFIG_BT_PAN_ENABLED
    return true;
#else
    return false;
#endif
  }
};
}  // namespace freeink
