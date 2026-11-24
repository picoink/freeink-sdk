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
  hid.onLinkUp("01:02:03:04:05:06", "Remote One", 0);
  freeink::turner::step();
  Turner::Event e{};
  report(true);
  report(false);
  assert(service.pop(e) && e.action == Turner::Action::Next);
  assert(!service.pop(e));
  command(Turner::Command::Learn, 1);
  report(true);
  assert(service.snapshot().learning == 1);
  assert(!service.pop(e));
  report(false);
  assert(!service.snapshot().learning);
  auto first = service.snapshot().bindings[0].codes[0];
  assert(first);
  report(true);
  report(false);
  assert(service.pop(e) && e.action == Turner::Action::Previous);
  // The same physical report has independent bindings on another remote.
  hid.onLinkDown();
  hid.onLinkUp("11:12:13:14:15:16", "Remote Two", 0);
  freeink::turner::step();
  command(Turner::Command::Learn, 3);
  report(true);
  report(false);
  assert(service.snapshot().bindings[0].codes[0] == first);
  assert(service.snapshot().bindings[1].codes[2]);
  report(true);
  report(false);
  assert(service.pop(e) && e.action == Turner::Action::Refresh);
  // Failed writes do not change a working binding or enabled preference.
  Preferences::saveOk = false;
  command(Turner::Command::Learn, 1);
  report(true);
  report(false);
  assert(!service.snapshot().bindings[1].codes[0]);
  assert(service.snapshot().bindings[1].codes[2]);
  command(Turner::Command::Enable, 0);
  assert(service.snapshot().enabled);
  Preferences::saveOk = true;
  // Cancellation wins even when a full command queue includes stale learning.
  Turner::Request r{};
  r.command = Turner::Command::Learn;
  r.value = 1;
  for (int i = 0; i < 7; ++i)
    assert(service.request(r));
  assert(!service.request(r));
  r.command = Turner::Command::CancelLearn;
  assert(service.request(r));
  freeink::turner::step();
  assert(!service.snapshot().learning);
  command(Turner::Command::Learn, 2);
  nowMs += 30001;
  freeink::turner::step();
  assert(!service.snapshot().learning);
  // Sleep waits for the worker's current-generation acknowledgement and GATT.
  assert(!service.suspend());
  assert(!service.request(r));
  freeink::turner::step();
  assert(!service.suspend());
  event(ESP_GATTC_UNREG_EVT);
  freeink::turner::step();
  assert(service.suspend());
  assert(!service.pop(e));
  service.resume();
  nowMs += 4001;
  freeink::turner::step();
  assert(service.snapshot().enabled && hid.isRunning());
  registered();
  // A second suspend cannot reuse the previous cycle's acknowledgement.
  assert(!service.suspend());
  freeink::turner::step();
  event(ESP_GATTC_UNREG_EVT);
  freeink::turner::step();
  assert(service.suspend());
  puts("Page turner per-device learning, input, persistence failure, "
       "cancellation and sleep generation tests passed");
}
