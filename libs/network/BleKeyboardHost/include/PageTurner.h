#pragma once
#include <BleKeyboardHost.h>
namespace freeink {
// Small service around the HID central. Commands/status are safe from the UI
// task; a single worker owns Bluetooth, preferences and report consumption.
class PageTurner {
public:
  enum class Command : uint8_t {
    Enable,
    Scan,
    Connect,
    Disconnect,
    Forget,
    Reconnect
  };
  struct Request {
    Command command{};
    uint8_t value = 0;
    char address[18]{};
  };
  struct Snapshot {
    bool supported = false, enabled = false, running = false, stopping = false,
         scanning = false, connected = false, connecting = false;
    uint8_t deviceCount = 0, pairedCount = 0;
    uint32_t passkey = 0;
    bool hasPasskey = false;
    char selected[18]{}, connectedAddress[18]{}, connectedName[32]{},
        message[96]{};
    DiscoveredDevice devices[BleKeyboardHost::kMaxDiscovered]{};
    PairedHidDevice paired[BleKeyboardHost::kMaxBonds]{};
  };
  struct Event {
    RawButtonEvent raw{};
    char address[18]{};
  };
  static PageTurner &instance();
  static bool supported();
  bool begin();
  bool request(const Request &);
  Snapshot snapshot();
  bool pop(Event &);
  // Assert before sleep and poll until true. While suspended, commands and
  // input are discarded. resume() also restores the preference after refusal.
  bool suspend();
  void resume();

private:
  PageTurner() = default;
};
} // namespace freeink
