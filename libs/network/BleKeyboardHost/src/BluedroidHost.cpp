// ESP-IDF Bluedroid HID central. PAN and HID hold independent BluetoothHost
// leases. Bluetooth callbacks only copy bounded events; poll() owns discovery,
// persistence and input decoding. No GATT wait runs on the Bluetooth task.
#include "BleKeyboardHost.h"
#include <BoardConfig.h>
#if FREEINK_CAP_BLE_HID_HOST && FREEINK_BLE_BLUEDROID
#include "HidKeymap.h"
#include <BluetoothHost.h>
#include <Preferences.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace freeink {
namespace {
constexpr uint16_t kApp = 0xF1, kNoConnection = 0xFFFF;
constexpr uint32_t kReleaseTimeoutMs = 150, kReconnectBackoffMs = 4000,
                   kConnectTimeoutMs = 8000;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
#include "HidReportState.inc"
bool g_connecting = false;
std::atomic<bool> stopping{false};
esp_gatt_if_t iface = ESP_GATT_IF_NONE;
uint16_t connection = kNoConnection, serviceStart = 0, serviceEnd = 0,
         mapHandle = 0, protocolHandle = 0;
esp_bd_addr_t peer{};
char peerText[18]{};
uint8_t peerType = 0;
bool bondRemovalPending = false;
uint8_t removingPeer[6]{};
bool registrationPending = false, unregisterPending = false,
     openPending = false;
bool scanConfigPending = false, scanStartPending = false, scanActive = false,
     scanStopPending = false;
bool cancelPending = false, closePending = false, encrypted = false,
     cancelled = false;
uint32_t scanDuration = 0, phaseSince = 0;
enum class Phase {
  Idle,
  Opening,
  Encrypting,
  Search,
  Map,
  Protocol,
  Reference,
  Subscribe,
  Cccd,
  Ready
};
Phase phase = Phase::Idle;
struct Input {
  uint16_t handle, reference, cccd;
  uint8_t id;
  bool enabled;
};
Input inputs[4]{};
uint8_t inputCount = 0, inputIndex = 0;

// Control traffic is separate from high-rate advertisements and notifications:
// a noisy peripheral cannot drop a disconnect or unregister acknowledgement.
enum class Kind {
  Register,
  Unregister,
  Open,
  Close,
  Cancel,
  SearchResult,
  SearchDone,
  Read,
  Write,
  Subscribe,
  Notify,
  ScanConfig,
  ScanStart,
  ScanStop,
  ScanDone,
  Advert,
  Security,
  Auth,
  Passkey,
  RejectPasskey,
  ServiceChanged,
  BondRemoved
};
struct Event {
  Kind kind{};
  esp_gatt_if_t interface = ESP_GATT_IF_NONE;
  uint16_t status = 0, conn = kNoConnection, handle = 0, end = 0, size = 0;
  uint32_t number = 0;
  uint8_t address[6]{}, type = 0;
  int8_t rssi = 0;
  bool flag = false;
  uint8_t data[512]{};
};
StaticQueue_t controlQueueStorage, dataQueueStorage;
uint8_t controlBytes[16 * sizeof(Event)], dataBytes[8 * sizeof(Event)];
QueueHandle_t controlQueue = nullptr, dataQueue = nullptr;
std::atomic<bool> accepting{false}, overflow{false};
void enqueue(const Event &e, bool data = false) {
  if (!accepting.load())
    return;
  if (xQueueSend(data ? dataQueue : controlQueue, &e, 0) != pdTRUE && !data)
    overflow = true;
}
bool matches(const uint8_t *address) { return memcmp(peer, address, 6) == 0; }
void addressText(const uint8_t *a, char *out) {
  snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3],
           a[4], a[5]);
}
bool parseAddress(const char *text, uint8_t *a) {
  if (!text || strlen(text) != 17)
    return false;
  for (unsigned i = 0; i < 6; ++i) {
    auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      return -1;
    };
    const int hi = hex(text[i * 3]), lo = hex(text[i * 3 + 1]);
    if (hi < 0 || lo < 0 || (i < 5 && text[i * 3 + 2] != ':'))
      return false;
    a[i] = uint8_t(hi * 16 + lo);
  }
  return true;
}
esp_bt_uuid_t uuid(uint16_t value) {
  esp_bt_uuid_t u{};
  u.len = ESP_UUID_LEN_16;
  u.uuid.uuid16 = value;
  return u;
}
void setPhase(Phase p) {
  phase = p;
  phaseSince = millis();
}
void gapCallback(esp_gap_ble_cb_event_t kind, esp_ble_gap_cb_param_t *p) {
  if (!accepting.load())
    return;
  Event e{};
  switch (kind) {
  case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT:
    e.kind = Kind::ScanConfig;
    e.status = p->set_ext_scan_params.status;
    break;
  case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:
    e.kind = Kind::ScanStart;
    e.status = p->ext_scan_start.status;
    break;
  case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT:
    e.kind = Kind::ScanStop;
    e.status = p->ext_scan_stop.status;
    break;
  case ESP_GAP_BLE_SCAN_TIMEOUT_EVT:
    e.kind = Kind::ScanDone;
    break;
  case ESP_GAP_BLE_EXT_ADV_REPORT_EVT: {
    const auto &a = p->ext_adv_report.params;
    e.kind = Kind::Advert;
    memcpy(e.address, a.addr, 6);
    e.type = a.addr_type;
    e.rssi = a.rssi;
    e.flag = (a.event_type & 1) != 0;
    e.size = std::min<size_t>(a.adv_data_len, sizeof(e.data));
    memcpy(e.data, a.adv_data, e.size);
    enqueue(e, true);
    return;
  }
  case ESP_GAP_BLE_SEC_REQ_EVT:
    e.kind = Kind::Security;
    memcpy(e.address, p->ble_security.ble_req.bd_addr, 6);
    break;
  case ESP_GAP_BLE_AUTH_CMPL_EVT:
    e.kind = Kind::Auth;
    memcpy(e.address, p->ble_security.auth_cmpl.bd_addr, 6);
    e.flag = p->ble_security.auth_cmpl.success;
    break;
  case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
    e.kind = Kind::Passkey;
    memcpy(e.address, p->ble_security.key_notif.bd_addr, 6);
    e.number = p->ble_security.key_notif.passkey;
    break;
  case ESP_GAP_BLE_PASSKEY_REQ_EVT:
    e.kind = Kind::RejectPasskey;
    memcpy(e.address, p->ble_security.ble_req.bd_addr, 6);
    break;
  case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
    e.kind = Kind::BondRemoved;
    memcpy(e.address, p->remove_bond_dev_cmpl.bd_addr, 6);
    e.status = p->remove_bond_dev_cmpl.status;
    break;
  case ESP_GAP_BLE_NC_REQ_EVT: // Display-only IO never silently accepts numeric
                               // comparison.
    esp_ble_confirm_reply(p->ble_security.key_notif.bd_addr, false);
    return;
  default:
    return;
  }
  enqueue(e);
}
void gattCallback(esp_gattc_cb_event_t kind, esp_gatt_if_t id,
                  esp_ble_gattc_cb_param_t *p) {
  if (!accepting.load())
    return;
  Event e{};
  e.interface = id;
  switch (kind) {
  case ESP_GATTC_REG_EVT:
    if (p->reg.app_id != kApp)
      return;
    e.kind = Kind::Register;
    e.status = p->reg.status;
    break;
  case ESP_GATTC_UNREG_EVT:
    e.kind = Kind::Unregister;
    break;
  case ESP_GATTC_OPEN_EVT:
    e.kind = Kind::Open;
    e.status = p->open.status;
    e.conn = p->open.conn_id;
    memcpy(e.address, p->open.remote_bda, 6);
    break;
  case ESP_GATTC_CLOSE_EVT:
    e.kind = Kind::Close;
    e.conn = p->close.conn_id;
    memcpy(e.address, p->close.remote_bda, 6);
    break;
  case ESP_GATTC_CANCEL_OPEN_EVT:
    e.kind = Kind::Cancel;
    e.status = p->cancel_open.status;
    break;
  case ESP_GATTC_SEARCH_RES_EVT:
    if (p->search_res.srvc_id.uuid.len != ESP_UUID_LEN_16 ||
        p->search_res.srvc_id.uuid.uuid.uuid16 != 0x1812)
      return;
    e.kind = Kind::SearchResult;
    e.conn = p->search_res.conn_id;
    e.handle = p->search_res.start_handle;
    e.end = p->search_res.end_handle;
    break;
  case ESP_GATTC_SEARCH_CMPL_EVT:
    e.kind = Kind::SearchDone;
    e.conn = p->search_cmpl.conn_id;
    e.status = p->search_cmpl.status;
    break;
  case ESP_GATTC_READ_CHAR_EVT:
  case ESP_GATTC_READ_DESCR_EVT:
    e.kind = Kind::Read;
    e.conn = p->read.conn_id;
    e.status = p->read.status;
    e.handle = p->read.handle;
    if (p->read.value_len > sizeof(e.data)) {
      e.status = ESP_GATT_INVALID_ATTR_LEN;
      break;
    }
    e.size = p->read.value_len;
    if (e.size)
      memcpy(e.data, p->read.value, e.size);
    break;
  case ESP_GATTC_WRITE_CHAR_EVT:
  case ESP_GATTC_WRITE_DESCR_EVT:
    e.kind = Kind::Write;
    e.conn = p->write.conn_id;
    e.status = p->write.status;
    e.handle = p->write.handle;
    break;
  case ESP_GATTC_REG_FOR_NOTIFY_EVT:
    e.kind = Kind::Subscribe;
    e.status = p->reg_for_notify.status;
    e.handle = p->reg_for_notify.handle;
    break;
  case ESP_GATTC_NOTIFY_EVT:
    if (p->notify.value_len > sizeof(e.data))
      return;
    e.kind = Kind::Notify;
    e.conn = p->notify.conn_id;
    e.handle = p->notify.handle;
    e.size = p->notify.value_len;
    memcpy(e.data, p->notify.value, e.size);
    enqueue(e, true);
    return;
  case ESP_GATTC_SRVC_CHG_EVT:
    e.kind = Kind::ServiceChanged;
    memcpy(e.address, p->srvc_chg.remote_bda, 6);
    break;
  default:
    return;
  }
  enqueue(e);
}
void closeLink() {
  cancelled = true;
  if (connection != kNoConnection && !closePending)
    closePending = esp_ble_gattc_close(iface, connection) == ESP_OK;
  else if (openPending && !cancelPending) {
    esp_ble_gattc_cancel_open_params_t p{};
    p.gattc_if = iface;
    memcpy(p.remote_bda, peer, 6);
    cancelPending = esp_ble_gattc_cancel_open(&p) == ESP_OK;
  }
}
void fail(const char *reason) {
  BleKeyboardHost::getInstance().onConnectFailed(reason);
  closeLink();
}
uint16_t descriptor(uint16_t handle, uint16_t id) {
  esp_gattc_descr_elem_t value{};
  uint16_t count = 1;
  return esp_ble_gattc_get_descr_by_char_handle(iface, connection, handle,
                                                uuid(id), &value,
                                                &count) == ESP_GATT_OK &&
                 count
             ? value.handle
             : 0;
}
void subscribeNext();
void registerInput() {
  setPhase(Phase::Subscribe);
  if (esp_ble_gattc_register_for_notify(iface, peer,
                                        inputs[inputIndex].handle) != ESP_OK)
    fail("Cannot subscribe to HID input");
}
void subscribeNext() {
  if (inputIndex == inputCount) {
    bool any = false;
    for (uint8_t i = 0; i < inputCount; ++i)
      any |= inputs[i].enabled;
    if (!any) {
      fail("No HID input report");
      return;
    }
    setPhase(Phase::Ready);
    g_connecting = false;
    BleKeyboardHost::getInstance().onLinkUp(peerText, nullptr, peerType);
    return;
  }
  auto &in = inputs[inputIndex];
  if (!in.cccd) {
    ++inputIndex;
    subscribeNext();
    return;
  }
  if (in.reference) {
    setPhase(Phase::Reference);
    if (esp_ble_gattc_read_char_descr(iface, connection, in.reference,
                                      ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
      fail("Cannot read HID report identity");
  } else
    registerInput();
}
void configureProtocol() {
  if (protocolHandle) {
    uint8_t report = 1;
    setPhase(Phase::Protocol);
    if (esp_ble_gattc_write_char(iface, connection, protocolHandle, 1, &report,
                                 ESP_GATT_WRITE_TYPE_NO_RSP,
                                 ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
      fail("Cannot select HID protocol");
  } else
    subscribeNext();
}
void discoverInputs() {
  esp_gattc_char_elem_t chars[32]{};
  uint16_t count = 32;
  if (!serviceStart ||
      esp_ble_gattc_get_all_char(iface, connection, serviceStart, serviceEnd,
                                 chars, &count, 0) != ESP_GATT_OK) {
    fail("HID service missing");
    return;
  }
  inputCount = inputIndex = 0;
  mapHandle = protocolHandle = 0;
  uint16_t boot = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const auto &c = chars[i];
    if (c.uuid.len != ESP_UUID_LEN_16)
      continue;
    const auto id = c.uuid.uuid.uuid16;
    if (id == 0x2A4B)
      mapHandle = c.char_handle;
    else if (id == 0x2A4E)
      protocolHandle = c.char_handle;
    else if (id == 0x2A22 && (c.properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY))
      boot = c.char_handle;
    else if (id == 0x2A4D && (c.properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) &&
             inputCount < 4) {
      auto &in = inputs[inputCount++];
      in = {c.char_handle, descriptor(c.char_handle, 0x2908),
            descriptor(c.char_handle, 0x2902), 0, false};
    }
  }
  if (!inputCount && boot) {
    inputs[inputCount++] = {boot, 0, descriptor(boot, 0x2902), 0, false};
    protocolHandle = 0;
  }
  if (!inputCount) {
    fail("No HID input characteristic");
    return;
  }
  if (mapHandle) {
    setPhase(Phase::Map);
    if (esp_ble_gattc_read_char(iface, connection, mapHandle,
                                ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
      fail("Cannot read HID report map");
  } else
    configureProtocol();
}
} // namespace

bool BleKeyboardHost::begin(const char *) {
  if (stopping)
    return false;
  if (begun_)
    return true;
  if (!controlQueue) {
    controlQueue = xQueueCreateStatic(16, sizeof(Event), controlBytes,
                                      &controlQueueStorage);
    dataQueue =
        xQueueCreateStatic(8, sizeof(Event), dataBytes, &dataQueueStorage);
  }
  xQueueReset(controlQueue);
  xQueueReset(dataQueue);
  overflow = false;
  if (!BluetoothHost::acquire(BluetoothHost::Profile::Hid)) {
    stopping = BluetoothHost::held(BluetoothHost::Profile::Hid);
    return false;
  }
  begun_ = true;
  accepting = true;
  g_autoReconnect = true;
  clearSelectedPlanLocked();
  loadBonds();
  uint8_t auth = ESP_LE_AUTH_BOND, io = ESP_IO_CAP_OUT,
          key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  if (esp_ble_gap_register_callback(gapCallback) != ESP_OK ||
      esp_ble_gattc_register_callback(gattCallback) != ESP_OK ||
      esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth,
                                     sizeof(auth)) != ESP_OK ||
      esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, sizeof(io)) !=
          ESP_OK ||
      esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &key,
                                     sizeof(key)) != ESP_OK ||
      esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &key,
                                     sizeof(key)) != ESP_OK) {
    end(0);
    return false;
  }
  registrationPending = esp_ble_gattc_app_register(kApp) == ESP_OK;
  if (!registrationPending) {
    end(0);
    return false;
  }
  return true;
}
bool BleKeyboardHost::isStopping() const { return stopping; }

// begin/end/poll and commands belong to one service task. Only popKey and
// popRawButton are cross-task consumers. This matches the Bluedroid event
// owner.
bool BleKeyboardHost::end(uint32_t timeoutMs) {
  if (!begun_ && !stopping)
    return true;
  stopping = true;
  begun_ = false;
  g_autoReconnect = false;
  clearSelectedPlanLocked();
  const auto start = millis();
  do {
    stopScan();
    closeLink();
    poll();
    if (!registrationPending && !bondRemovalPending && !scanConfigPending &&
        !scanStartPending && !scanActive && !scanStopPending && !openPending &&
        connection == kNoConnection) {
      if (iface != ESP_GATT_IF_NONE && !unregisterPending)
        unregisterPending = esp_ble_gattc_app_unregister(iface) == ESP_OK;
      if (iface == ESP_GATT_IF_NONE && !unregisterPending) {
        accepting = false;
        if (!BluetoothHost::release(BluetoothHost::Profile::Hid))
          return false;
        stopping = false;
        cancelled = false;
        g_connecting = false;
        phase = Phase::Idle;
        onLinkDown();
        scanning_ = false;
        deviceCount_ = 0;
        portENTER_CRITICAL(&g_mux);
        ringHead_ = 0;
        ringTail_ = 0;
        rawHead_ = 0;
        rawTail_ = 0;
        portEXIT_CRITICAL(&g_mux);
        return true;
      }
    }
    if (millis() - start >= std::min<uint32_t>(timeoutMs, 2000))
      return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  } while (true);
}

void BleKeyboardHost::poll() {
  if (!begun_ && !stopping)
    return;
  Event e{};
  // Bound work per poll, giving UI/control work a turn even on a busy radio.
  for (unsigned n = 0; n < 24; ++n) {
    if (xQueueReceive(controlQueue, &e, 0) != pdTRUE &&
        xQueueReceive(dataQueue, &e, 0) != pdTRUE)
      break;
    if (e.interface != ESP_GATT_IF_NONE && e.kind != Kind::Register &&
        e.interface != iface)
      continue;
    switch (e.kind) {
    case Kind::Register:
      registrationPending = false;
      if (!e.status)
        iface = e.interface;
      else {
        onConnectFailed("Cannot register HID client");
        stopping = true;
        begun_ = false;
      }
      break;
    case Kind::Unregister:
      iface = ESP_GATT_IF_NONE;
      unregisterPending = false;
      break;
    case Kind::ScanConfig:
      scanConfigPending = false;
      if (!e.status && scanning_ && !stopping)
        scanStartPending =
            esp_ble_gap_start_ext_scan(scanDuration / 10, 0) == ESP_OK;
      if (!scanStartPending)
        scanning_ = false;
      break;
    case Kind::ScanStart:
      scanStartPending = false;
      scanActive = !e.status;
      if (!scanActive)
        scanning_ = false;
      break;
    case Kind::ScanStop:
      scanStopPending = false;
      if (!e.status)
        scanActive = false;
      break;
    case Kind::ScanDone:
      scanActive = false;
      scanning_ = false;
      break;
    case Kind::Advert: {
      if (!scanning_ || stopping)
        break;
      char name[32]{}, addr[18];
      bool hid = false;
      addressText(e.address, addr);
      for (size_t i = 0; i < e.size;) {
        size_t len = e.data[i];
        if (!len || i + len >= e.size)
          break;
        const auto tag = e.data[i + 1];
        const auto *bytes = e.data + i + 2;
        if (tag == 0x08 || tag == 0x09) {
          size_t take = std::min<size_t>(len - 1, 31);
          memcpy(name, bytes, take);
          name[take] = 0;
        }
        if (tag == 0x02 || tag == 0x03)
          for (size_t j = 0; j + 1 < len - 1; j += 2)
            hid |= (bytes[j] == 0x12 && bytes[j + 1] == 0x18);
        if (tag == 0x19 && len >= 3)
          hid |= (bytes[0] == 0xC1 && bytes[1] == 0x03);
        i += len + 1;
      }
      onScanResultIngest(addr, name, e.rssi, e.type, hid, e.flag);
      break;
    }
    case Kind::Open:
      if (!matches(e.address))
        break;
      openPending = false;
      cancelPending = false;
      if (e.status) {
        g_connecting = false;
        setPhase(Phase::Idle);
        onConnectFailed("HID connection failed");
        break;
      }
      connection = e.conn;
      if (stopping || cancelled) {
        closeLink();
        break;
      }
      setPhase(Phase::Encrypting);
      if (esp_ble_set_encryption(peer, ESP_BLE_SEC_ENCRYPT_NO_MITM) != ESP_OK)
        fail("Cannot secure HID connection");
      break;
    case Kind::Close:
      if (!matches(e.address) || e.conn != connection)
        break;
      connection = kNoConnection;
      openPending = cancelPending = closePending = false;
      g_connecting = false;
      encrypted = false;
      inputCount = 0;
      cancelled = false;
      onLinkDown();
      setPhase(Phase::Idle);
      break;
    case Kind::Cancel:
      cancelPending = false;
      if (!e.status) {
        openPending = false;
        g_connecting = false;
        connecting_ = false;
        cancelled = false;
        setPhase(Phase::Idle);
      }
      break;
    case Kind::Security:
      esp_ble_gap_security_rsp(e.address, matches(e.address) &&
                                              connection != kNoConnection &&
                                              !stopping && !cancelled);
      break;
    case Kind::RejectPasskey:
      esp_ble_passkey_reply(e.address, false, 0);
      if (matches(e.address))
        fail("Remote requires passkey input");
      break;
    case Kind::Passkey:
      if (matches(e.address) && !stopping && !cancelled)
        onPairingPasskey(e.number);
      break;
    case Kind::Auth:
      if (!matches(e.address) || phase != Phase::Encrypting || stopping ||
          cancelled)
        break;
      if (!e.flag) {
        fail("HID pairing failed");
        break;
      }
      encrypted = true;
      serviceStart = serviceEnd = 0;
      setPhase(Phase::Search);
      {
        auto service = uuid(0x1812);
        if (esp_ble_gattc_search_service(iface, connection, &service) != ESP_OK)
          fail("Cannot discover HID service");
      }
      break;
    case Kind::SearchResult:
      if (e.conn == connection && phase == Phase::Search) {
        serviceStart = e.handle;
        serviceEnd = e.end;
      }
      break;
    case Kind::SearchDone:
      if (e.conn == connection && phase == Phase::Search && !stopping &&
          !cancelled) {
        if (e.status)
          fail("HID discovery failed");
        else
          discoverInputs();
      }
      break;
    case Kind::Read:
      if (e.conn != connection || stopping || cancelled)
        break;
      if (phase == Phase::Map && e.handle == mapHandle) {
        if (e.status) {
          fail("HID report map failed");
          break;
        }
        parseReportMapHints(e.data, e.size);
        g_bareKeys = bareKeysOf(e.data, e.size);
        configureProtocol();
      } else if (phase == Phase::Reference &&
                 e.handle == inputs[inputIndex].reference) {
        if (e.status || e.size != 2) {
          fail("Invalid HID report identity");
          break;
        }
        if (e.data[1] != 1) {
          ++inputIndex;
          subscribeNext();
          break;
        }
        inputs[inputIndex].id = e.data[0];
        registerInput();
      }
      break;
    case Kind::Write:
      if (e.conn != connection || stopping || cancelled)
        break;
      if (phase == Phase::Protocol && e.handle == protocolHandle) {
        if (e.status)
          fail("HID protocol rejected");
        else
          subscribeNext();
      } else if (phase == Phase::Cccd && e.handle == inputs[inputIndex].cccd) {
        if (e.status) {
          fail("HID notifications rejected");
          break;
        }
        inputs[inputIndex].enabled = true;
        ++inputIndex;
        subscribeNext();
      }
      break;
    case Kind::Subscribe:
      if (phase != Phase::Subscribe || stopping || cancelled ||
          e.handle != inputs[inputIndex].handle)
        break;
      if (e.status) {
        fail("HID subscription failed");
        break;
      }
      {
        uint8_t enable[2] = {1, 0};
        setPhase(Phase::Cccd);
        if (esp_ble_gattc_write_char_descr(
                iface, connection, inputs[inputIndex].cccd, 2, enable,
                ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
          fail("Cannot enable HID notifications");
      }
      break;
    case Kind::Notify:
      if (e.conn != connection || phase != Phase::Ready || !encrypted ||
          stopping || cancelled)
        break;
      for (uint8_t i = 0; i < inputCount; ++i)
        if (inputs[i].enabled && inputs[i].handle == e.handle) {
          g_notifyReportId = inputs[i].id;
          onReportIngest(e.data, e.size);
          break;
        }
      break;
    case Kind::BondRemoved:
      if (!bondRemovalPending || memcmp(e.address, removingPeer, 6))
        break;
      bondRemovalPending = false;
      if (e.status) {
        onConnectFailed("Cannot remove HID pairing");
        break;
      }
      {
        char addr[18];
        addressText(e.address, addr);
        for (uint8_t i = 0; i < bondCount_; ++i)
          if (!strcmp(bonds_[i].addr, addr)) {
            for (uint8_t j = i + 1; j < bondCount_; ++j)
              bonds_[j - 1] = bonds_[j];
            --bondCount_;
            persistBonds();
            break;
          }
      }
      break;
    case Kind::ServiceChanged:
      if (matches(e.address)) {
        esp_ble_gattc_cache_refresh(peer);
        fail("HID service changed; reconnect");
      }
      break;
    }
  }
  if (overflow.exchange(false))
    fail("HID control queue overflow");
  if ((stopping || !scanning_) && scanActive)
    stopScan();
  if (stopping || cancelled) {
    closeLink();
    return;
  }
  if (g_connecting && millis() - phaseSince >= kConnectTimeoutMs)
    fail("HID connection timed out");
#include "HidHostPoll.inc"
}
void BleKeyboardHost::startScan(uint32_t ms) {
  if (!begun_ || stopping || scanning_ || g_connecting || connected_ ||
      scanActive || scanConfigPending || scanStartPending)
    return;
  g_autoReconnect = false;
  clearSelectedPlanLocked();
  deviceCount_ = 0;
  esp_ble_ext_scan_params_t p{};
  p.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  p.filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
  p.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
  p.cfg_mask =
      ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK | ESP_BLE_GAP_EXT_SCAN_CFG_CODE_MASK;
  p.uncoded_cfg = {BLE_SCAN_TYPE_ACTIVE, 160, 160};
  p.coded_cfg = p.uncoded_cfg;
  scanDuration = std::max<uint32_t>(1000, std::min<uint32_t>(30000, ms));
  scanning_ = scanConfigPending = esp_ble_gap_set_ext_scan_params(&p) == ESP_OK;
}
void BleKeyboardHost::stopScan() {
  scanning_ = false;
  if (scanActive && !scanStopPending)
    scanStopPending = esp_ble_gap_stop_ext_scan() == ESP_OK;
}
const DiscoveredDevice &BleKeyboardHost::device(uint8_t i) const {
  static const DiscoveredDevice empty{};
  return i < deviceCount_ ? devices_[i] : empty;
}
void BleKeyboardHost::releaseScanResults() { deviceCount_ = 0; }
const PairedHidDevice &BleKeyboardHost::paired(uint8_t i) const {
  static const PairedHidDevice empty{};
  return i < bondCount_ ? bonds_[i] : empty;
}
bool BleKeyboardHost::connect(const char *addr) {
  return connectInternal(addr, true);
}
bool BleKeyboardHost::connectInternal(const char *addr, bool explicitRequest) {
  uint8_t target[6];
  if (!begun_ || stopping || iface == ESP_GATT_IF_NONE || g_connecting ||
      connected_ || openPending || connection != kNoConnection ||
      !parseAddress(addr, target))
    return false;
  if (explicitRequest) {
    clearSelectedPlanLocked();
    g_autoReconnect = true;
  }
  stopScan();
  peerType = BLE_ADDR_TYPE_PUBLIC;
  for (uint8_t i = 0; i < deviceCount_; ++i)
    if (!strcmp(devices_[i].addr, addr))
      peerType = devices_[i].addrType;
  for (uint8_t i = 0; i < bondCount_; ++i)
    if (!strcmp(bonds_[i].addr, addr))
      peerType = bonds_[i].addrType;
  xQueueReset(dataQueue);
  memcpy(peer, target, 6);
  addressText(peer, peerText);
  cancelled = false;
  encrypted = false;
  inputCount = inputIndex = 0;
  g_bareKeys = {};
  g_hasKeyboardPage = g_hasConsumerPage = false;
  g_preferredByteIndex = 0xFF;
  g_lastGenericCode = 0;
  g_lastReportMs = 0;
  g_lastReconnectMs = millis();
  connectFailed_ = false;
  pairingPasskeyReady_ = false;
  connectFailure_[0] = 0;
  esp_ble_gatt_creat_conn_params_t p{};
  p.remote_addr_type = static_cast<esp_ble_addr_type_t>(peerType);
  memcpy(p.remote_bda, peer, 6);
  p.is_direct = true;
  p.is_aux = true;
  openPending = esp_ble_gattc_enh_open(iface, &p) == ESP_OK;
  connecting_ = g_connecting = openPending;
  if (openPending)
    setPhase(Phase::Opening);
  else
    onConnectFailed("Cannot start HID connection");
  return openPending;
}
bool BleKeyboardHost::armSelectedPeerReconnect(const char *addr) {
  if (!addr || !begun_ || stopping || connected_ || g_connecting || scanning_ ||
      g_selectedAddr[0])
    return false;
  bool found = false;
  for (uint8_t i = 0; i < bondCount_; ++i)
    found |= !strcmp(bonds_[i].addr, addr);
  if (!found)
    return false;
  strncpy(g_selectedAddr, addr, sizeof(g_selectedAddr) - 1);
  g_selectedAttemptsLeft = kSelectedReconnectAttempts;
  g_selectedStartedMs = millis();
  g_lastReconnectMs = g_selectedStartedMs - kReconnectBackoffMs;
  cancelled = false;
  return true;
}
void BleKeyboardHost::disconnect() {
  g_autoReconnect = false;
  clearSelectedPlanLocked();
  closeLink();
}
void BleKeyboardHost::forget(const char *addr) {
  uint8_t a[6];
  if (!begun_ || stopping || bondRemovalPending || !parseAddress(addr, a))
    return;
  if (matches(a))
    disconnect();
  if (!strcmp(g_selectedAddr, addr))
    clearSelectedPlanLocked();
  bondRemovalPending = esp_ble_remove_bond_device(a) == ESP_OK;
  if (bondRemovalPending)
    memcpy(removingPeer, a, 6);
  else
    onConnectFailed("Cannot remove HID pairing");
}
#include "HidHostCommon.inc"
} // namespace freeink
#endif
