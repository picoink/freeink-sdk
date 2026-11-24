#include <BluetoothHost.h>
#if defined(ARDUINO_ARCH_ESP32) && !defined(FREEINK_HOST)
#include <sdkconfig.h>
#if CONFIG_BT_BLUEDROID_ENABLED
#include <esp32-hal-bt.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <mutex>
namespace {
std::mutex mutex;
uint8_t owners = 0;
bool stopping = false, controllerOwned = false;
bool releaseLast() {
  stopping = true;
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED &&
      esp_bluedroid_disable() != ESP_OK)
    return false;
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED &&
      esp_bluedroid_deinit() != ESP_OK)
    return false;
  if (controllerOwned && !btStop())
    return false;
  owners = 0;
  stopping = controllerOwned = false;
  return true;
}
} // namespace
namespace freeink {
bool BluetoothHost::acquire(Profile profile) {
  std::lock_guard<std::mutex> guard(mutex);
  const auto bit = uint8_t(profile);
  if (stopping)
    return false;
  if (owners) {
    owners |= bit;
    return true;
  }
  // Never take ownership of a controller initialized by an unrelated service.
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE ||
      esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED)
    return false;
  owners = bit;
  controllerOwned = true;
#if CONFIG_BT_CLASSIC_ENABLED && CONFIG_BT_BLE_ENABLED
  constexpr auto mode = BT_MODE_BTDM;
#elif CONFIG_BT_CLASSIC_ENABLED
  constexpr auto mode = BT_MODE_CLASSIC_BT;
#else
  constexpr auto mode = BT_MODE_BLE;
#endif
  if (!btStartMode(mode) || esp_bluedroid_init() != ESP_OK ||
      esp_bluedroid_enable() != ESP_OK) {
    releaseLast();
    return false;
  }
  return true;
}
bool BluetoothHost::release(Profile profile) {
  std::lock_guard<std::mutex> guard(mutex);
  const auto bit = uint8_t(profile);
  if (!(owners & bit))
    return true;
  if (owners & ~bit) {
    owners &= ~bit;
    return true;
  }
  return releaseLast();
}
bool BluetoothHost::held(Profile profile) {
  std::lock_guard<std::mutex> guard(mutex);
  return owners & uint8_t(profile);
}
} // namespace freeink
#else
#define FREEINK_BLUETOOTH_HOST_STUB 1
#endif
#else
#define FREEINK_BLUETOOTH_HOST_STUB 1
#endif
#ifdef FREEINK_BLUETOOTH_HOST_STUB
namespace freeink {
bool BluetoothHost::acquire(Profile) { return false; }
bool BluetoothHost::release(Profile) { return true; }
bool BluetoothHost::held(Profile) { return false; }
} // namespace freeink
#endif
