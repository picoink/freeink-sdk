#pragma once
#include <stdint.h>
namespace freeink {
// Profile leases for the shared Bluedroid/controller instance. Profile
// callbacks and connections must be drained before release. Calls serialize
// initialization and teardown; use them from service tasks, never from
// Bluetooth callbacks.
class BluetoothHost {
public:
  enum class Profile : uint8_t { Pan = 1, Hid = 2 };
  static bool acquire(Profile);
  // False retains the last lease while cleanup is incomplete. Retry release;
  // acquire is refused until that cleanup completes. Other profiles stay live.
  static bool release(Profile);
  static bool held(Profile);
};
} // namespace freeink
