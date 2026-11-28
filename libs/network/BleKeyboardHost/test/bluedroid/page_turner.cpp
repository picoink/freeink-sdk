#define main transportScenarios
#include "lifecycle.cpp"
#undef main
#include "../../src/PageTurner.cpp"
using Turner = freeink::PageTurner;
static auto &service = Turner::instance();
static auto &hid = BleKeyboardHost::getInstance();
static void command(Turner::Command c, unsigned value = 0) {
  Turner::Request r{};
  r.command = c;
  r.value = value;
  assert(service.request(r));
  freeink::turner::step();
}
static void report(bool pressed) {
  uint8_t r[8] = {0, 0, uint8_t(pressed ? 0x4F : 0), 0, 0, 0, 0, 0};
  hid.onReportIngest(r, 8);
  freeink::turner::step();
}
int main() {
  assert(service.begin());
  freeink::turner::step();
  assert(!service.snapshot().enabled);
  command(Turner::Command::Enable, 1);
  registered();
  freeink::turner::step();
  Turner::Request select{};
  select.command = Turner::Command::Connect;
  strcpy(select.address, "01:02:03:04:05:06");
  assert(service.request(select));
  freeink::turner::step();
  // Only a completed link becomes the active device.
  assert(!service.snapshot().selected[0]);
  completeDiscovery(hid);
  freeink::turner::step();
  assert(!strcmp(service.snapshot().selected, select.address));
  Turner::Event e{};
  report(true);
  report(false);
  assert(service.pop(e) && e.raw.pressed && e.raw.keycode == 0x4F &&
         !strcmp(e.address, select.address));
  assert(service.pop(e) && !e.raw.pressed && !service.pop(e));
  // A second peer never removes the previous bond.
  strcpy(select.address, "11:12:13:14:15:16");
  assert(service.request(select));
  freeink::turner::step();
  assert(!strcmp(service.snapshot().selected, "01:02:03:04:05:06"));
  closed();
  freeink::turner::step();
  completeDiscovery(hid);
  freeink::turner::step();
  assert(service.snapshot().pairedCount == 2 &&
         !strcmp(service.snapshot().selected, select.address));
  // A failed handoff returns to the selected radio peer and preserves both
  // bonds.
  Turner::Request missing = select;
  strcpy(missing.address, "aa:bb:cc:dd:ee:ff");
  assert(service.request(missing));
  freeink::turner::step();
  closed();
  freeink::turner::step();
  esp_ble_gattc_cb_param_t failure{};
  memcpy(failure.open.remote_bda, peer, 6);
  failure.open.status = 1;
  event(ESP_GATTC_OPEN_EVT, failure);
  freeink::turner::step();
  freeink::turner::step();
  freeink::turner::step();
  assert(!strcmp(peerText, select.address));
  completeDiscovery(hid);
  freeink::turner::step();
  assert(service.snapshot().pairedCount == 2 &&
         !strcmp(service.snapshot().connectedAddress, select.address));
  Preferences::saveOk = false;
  command(Turner::Command::Enable, 0);
  assert(service.snapshot().enabled);
  Preferences::saveOk = true;
  Turner::Request r{};
  // Sleep waits for the worker's current-generation acknowledgement and GATT.
  assert(!service.suspend());
  assert(!service.request(r));
  freeink::turner::step();
  assert(!service.suspend());
  closed();
  freeink::turner::step();
  event(ESP_GATTC_UNREG_EVT);
  freeink::turner::step();
  assert(service.suspend());
  assert(!service.pop(e));
  service.resume();
  nowMs += 4001;
  freeink::turner::step();
  assert(service.snapshot().enabled && hid.isRunning());
  registered();
  // Reconnect may already have issued an open. Shutdown waits for its
  // cancellation.
  assert(hid.isConnecting());
  // A second suspend cannot reuse the previous cycle's acknowledgement.
  assert(!service.suspend());
  freeink::turner::step();
  event(ESP_GATTC_CANCEL_OPEN_EVT);
  freeink::turner::step();
  event(ESP_GATTC_UNREG_EVT);
  freeink::turner::step();
  assert(service.suspend());
  puts("Page turner exclusive connections, raw input, persistence failure, "
       "cancellation and sleep generation tests passed");
}
