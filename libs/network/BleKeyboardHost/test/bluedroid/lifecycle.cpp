#include "../../src/BluedroidHost.cpp"
#include "fakes.h"
#include <cassert>
using namespace freeink;
static void event(int id, esp_ble_gattc_cb_param_t p = {}) {
  gattCb(id, 1, &p);
  BleKeyboardHost::getInstance().poll();
}
static void gap(int id, esp_ble_gap_cb_param_t p = {}) {
  gapCb(id, &p);
  BleKeyboardHost::getInstance().poll();
}
static void registered() {
  esp_ble_gattc_cb_param_t p{};
  p.reg.app_id = 0xF1;
  event(ESP_GATTC_REG_EVT, p);
}
static void closed() {
  esp_ble_gattc_cb_param_t p{};
  p.close.conn_id = 7;
  memcpy(p.close.remote_bda, peer, 6);
  event(ESP_GATTC_CLOSE_EVT, p);
}
int main() {
  auto &host = BleKeyboardHost::getInstance();
  BluetoothHost::acquire(BluetoothHost::Profile::Pan);
  failEventAllocation=true;
  assert(!host.begin());
  assert(BluetoothHost::leases==1 && !accepting.load());
  failEventAllocation=false;
  assert(host.begin());
  assert(eventAllocationCaps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  assert(eventAllocationBytes==24*sizeof(Event));
  assert(!host.connect("broken"));
  registered();
  // Stop while configuring a scan: late start must never outlive the lease.
  host.startScan();
  assert(host.isScanning());
  assert(!host.end(0));
  gap(ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT);
  assert(!host.end(0));
  event(ESP_GATTC_UNREG_EVT);
  assert(host.end(0));
  assert(BluetoothHost::leases == 1);
  assert(host.begin());
  registered();
  assert(host.connect("01:02:03:04:05:06"));
  esp_ble_gattc_cb_param_t p{};
  p.open.conn_id = 7;
  memcpy(p.open.remote_bda, peer, 6);
  event(ESP_GATTC_OPEN_EVT, p);
  assert(!host.isConnected());
  esp_ble_gap_cb_param_t a{};
  memcpy(a.ble_security.auth_cmpl.bd_addr, peer, 6);
  a.ble_security.auth_cmpl.success = true;
  gap(ESP_GAP_BLE_AUTH_CMPL_EVT, a);
  p = {};
  p.search_res.conn_id = 7;
  p.search_res.start_handle = 10;
  p.search_res.end_handle = 30;
  p.search_res.srvc_id.uuid = uuid(0x1812);
  event(ESP_GATTC_SEARCH_RES_EVT, p);
  p = {};
  p.search_cmpl.conn_id = 7;
  event(ESP_GATTC_SEARCH_CMPL_EVT, p);
  uint8_t map[] = {0x05, 0x07};
  p = {};
  p.read.conn_id = 7;
  p.read.handle = 11;
  p.read.value = map;
  p.read.value_len = 2;
  event(ESP_GATTC_READ_CHAR_EVT, p);
  p = {};
  p.write.conn_id = 7;
  p.write.handle = 13;
  event(ESP_GATTC_WRITE_CHAR_EVT, p);
  uint8_t ref[] = {3, 1};
  p = {};
  p.read.conn_id = 7;
  p.read.handle = 21;
  p.read.value = ref;
  p.read.value_len = 2;
  event(ESP_GATTC_READ_DESCR_EVT, p);
  p = {};
  p.reg_for_notify.handle = 12;
  event(ESP_GATTC_REG_FOR_NOTIFY_EVT, p);
  assert(!host.isConnected());
  p = {};
  p.write.conn_id = 7;
  p.write.handle = 22;
  event(ESP_GATTC_WRITE_DESCR_EVT, p);
  assert(host.isConnected());
  assert(host.pairedCount() == 1);
  uint8_t report[] = {0, 0, 0x4F, 0, 0, 0, 0, 0};
  p = {};
  p.notify.conn_id = 7;
  p.notify.handle = 12;
  p.notify.value = report;
  p.notify.value_len = 8;
  event(ESP_GATTC_NOTIFY_EVT, p);
  KeyEvent key{};
  assert(host.popKey(key) && key.special == SpecialKey::Right);
  assert(!host.popKey(key));
  RawButtonEvent raw{};
  assert(host.popRawButton(raw) && raw.pressed && raw.reportId == 3);
  // Notification flood cannot hide control acknowledgements.
  for (int i = 0; i < 32; ++i)
    gattCb(ESP_GATTC_NOTIFY_EVT, 1, &p);
  assert(!host.end(0));
  closed();
  assert(!host.end(0));
  event(ESP_GATTC_UNREG_EVT);
  BluetoothHost::releaseOk = false;
  assert(!host.end(0));
  assert(host.isStopping());
  assert(!host.begin());
  BluetoothHost::releaseOk = true;
  assert(host.end(0));
  assert(BluetoothHost::leases == 1);
  // Cancel-before-open, followed by a racing successful open, is closed first.
  assert(host.begin());
  registered();
  assert(host.connect("01:02:03:04:05:06"));
  assert(!host.end(0));
  p = {};
  p.open.conn_id = 7;
  memcpy(p.open.remote_bda, peer, 6);
  event(ESP_GATTC_OPEN_EVT, p);
  assert(!host.isConnected());
  assert(!host.end(0));
  closed();
  assert(!host.end(0));
  event(ESP_GATTC_UNREG_EVT);
  assert(host.end(0));
  // Timeouts retain the transport until cancel/close acknowledges.
  assert(host.begin());
  registered();
  assert(host.connect("01:02:03:04:05:06"));
  nowMs += 8001;
  host.poll();
  char error[48];
  assert(host.takeConnectFailure(error, sizeof(error)));
  assert(!host.end(0));
  event(ESP_GATTC_CANCEL_OPEN_EVT);
  assert(!host.end(0));
  event(ESP_GATTC_UNREG_EVT);
  assert(host.end(0));
  puts("Bluedroid HID discovery, input, timeout and shared-host teardown tests "
       "passed");
  return 0;
}
