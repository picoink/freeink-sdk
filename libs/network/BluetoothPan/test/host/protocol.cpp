#include <BluetoothPan.h>

#include <cassert>
#include <cstring>

#include "../../src/PanProtocol.h"
using namespace freeink::pan_detail;
int main() {
  uint8_t bytes[1515]{};
  assert(!frame(nullptr, 100));
  assert(!frame(bytes, 14));
  assert(frame(bytes, 15) && frame(bytes, 1514));
  assert(!frame(bytes, 1515));
  assert(!addressValid(nullptr) && !addressValid(bytes));
  memset(bytes, 255, 6);
  assert(!addressValid(bytes));
  bytes[0] = 1;
  assert(addressValid(bytes));
  Retry retry;
  retry.schedule(100);
  assert(!retry.due(50000));
  retry.enabled = true;
  assert(!retry.due(2099) && retry.due(2100));
  retry.schedule(2100);
  assert(!retry.due(7099) && retry.due(7100));
  retry.schedule(7100);
  assert(!retry.due(17099) && retry.due(17100));
  for (int i = 0; i < 300; ++i) retry.schedule(17100);
  assert(retry.delay == 30000 && retry.attempts == 3);
  retry.schedule(0xfffffff0);
  assert(!retry.due(0x10));
  assert(retry.due(uint32_t(0xfffffff0 + 30000)));
  retry.enabled = false;
  assert(!retry.due(100000));
  using Pan = freeink::BluetoothPan;
  auto& pan = Pan::instance();
  assert(!pan.supported() && !pan.status().enabled);
  assert(pan.begin() == Pan::Result::Unsupported &&
         pan.end() == Pan::Result::Unsupported);
  assert(pan.scan() == Pan::Result::Unsupported &&
         pan.connect(bytes) == Pan::Result::Unsupported);
  assert(pan.disconnect() == Pan::Result::Unsupported &&
         pan.forget() == Pan::Result::Unsupported);
  assert(pan.confirmPairing(true) == Pan::Result::Unsupported &&
         !pan.status().enabled);
}
