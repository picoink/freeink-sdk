// FreeInk SDK — BLE HID host implementation.
//
// The real NimBLE central path compiles only under FREEINK_CAP_BLE_HID_HOST; the
// #else branch links stub bodies (and references no BLE code). Flow: scan ->
// connect on a dedicated FreeRTOS task -> bond -> set Report
// Protocol -> subscribe to the HID input report -> diff reports into key events.
// Connection runs on its own task so neither the main loop nor the NimBLE host
// task ever blocks on pairing.

#include "BleKeyboardHost.h"

#include <BoardConfig.h>  // FREEINK_CAP_BLE_HID_HOST

namespace freeink {

BleKeyboardHost& BleKeyboardHost::getInstance() {
  static BleKeyboardHost instance;
  return instance;
}

}  // namespace freeink

#if FREEINK_CAP_BLE_HID_HOST && !FREEINK_BLE_BLUEDROID

#include <NimBLEDevice.h>
#include <Preferences.h>

#include <atomic>
#include <cstring>
#include <string>

#include "HidKeymap.h"

namespace freeink {
namespace {

constexpr uint16_t kHidService = 0x1812;
constexpr uint16_t kAppearanceKeyboard = 0x03C1;
constexpr uint16_t kCharReport = 0x2A4D;
constexpr uint16_t kCharProtocolMode = 0x2A4E;
constexpr uint16_t kCharBootKbdInput = 0x2A22;
constexpr uint16_t kCharReportMap = 0x2A4B;
constexpr uint16_t kDescReportReference = 0x2908;
// Input report characteristics whose Report Reference id is kept for raw edges.
constexpr uint8_t kMaxInputReportChars = 4;

// Page-turner remotes often stream a held key (or omit a clean release frame). If no
// report arrives within this window, treat the key as released so one physical press
// yields one event and the next press re-triggers.
constexpr uint32_t kReleaseTimeoutMs = 150;
constexpr uint32_t kReconnectBackoffMs = 4000;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kTeardownConnectWaitMs = kConnectTimeoutMs + 500;
constexpr uint32_t kMaxTeardownTimeoutMs = 2000;
constexpr uint32_t kTeardownPollMs = 10;
constexpr size_t kScanDebugPayloadMax = 31;

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

NimBLEClient* g_client = nullptr;

// Connection task: connect() stores a target and notifies it; it runs the
// blocking connect+pair+discover sequence off the main and NimBLE host tasks.
TaskHandle_t g_connTask = nullptr;
volatile bool g_connecting = false;
char g_targetAddr[18] = {0};
uint8_t g_targetType = 0;
bool g_targetTryAltType = false;
// Set by an end() that could not finish; cleared by the one that does.
std::atomic<bool> g_teardownPending{false};
// Held by the one end() that is running. end() is called from more than one task
// (an app may stop the radio from its main loop and from a render task), and its
// teardown (connection task delete, client delete, NimBLEDevice::deinit) is not
// re-entrant: two at once can double-free NimBLE state. A second caller does not
// wait; it gets false, which reads as "still stopping" and is retried like a
// teardown that ran out of time.
std::atomic<bool> g_endBusy{false};
// False while a link attempt or a link exists; set again by NimBLE's disconnect
// callback. NimBLE only lets go of the client once that callback has run.
std::atomic<bool> g_disconnectObserved{true};

#include "HidReportState.inc"

// Report id each subscribed Input characteristic declares in its Report Reference
// descriptor, so a report keeps its identity when its payload has no id byte.
struct InputReportChar {
  const NimBLERemoteCharacteristic* chr;
  uint8_t reportId;
};
InputReportChar g_inputChars[kMaxInputReportChars];
uint8_t g_inputCharCount = 0;
BleKeyboardHost& self() { return BleKeyboardHost::getInstance(); }

// The client can be deleted: its disconnect callback ran and NimBLE reports it
// DISCONNECTED. deleteClient() on a CONNECTED or DISCONNECTING client only
// defers the delete, and a deinit() after that leaks NimBLE's client slot.
bool clientReleased() {
  return g_disconnectObserved.load() && NimBLEDevice::getDisconnectedClient() == g_client;
}

#ifndef FREEINK_BLE_HID_SCAN_DEBUG
#ifdef FREEINK_BLE_KEYBOARD_SCAN_DEBUG
#define FREEINK_BLE_HID_SCAN_DEBUG FREEINK_BLE_KEYBOARD_SCAN_DEBUG
#else
#define FREEINK_BLE_HID_SCAN_DEBUG 0
#endif
#endif

// Raw HID report logging. Define FREEINK_BLE_HID_REPORT_DEBUG=1 in the firmware to
// dump every notification's bytes — the fastest way to learn what a new remote sends.
#ifndef FREEINK_BLE_HID_REPORT_DEBUG
#define FREEINK_BLE_HID_REPORT_DEBUG 0
#endif

#if FREEINK_BLE_HID_SCAN_DEBUG
void printPayloadHex(const NimBLEAdvertisedDevice* dev) {
  if (!dev) return;
  const std::vector<uint8_t>& payload = dev->getPayload();
  const size_t n = payload.size() < kScanDebugPayloadMax ? payload.size() : kScanDebugPayloadMax;
  Serial.print("  payload=");
  for (size_t i = 0; i < n; ++i) {
    if (payload[i] < 0x10) Serial.print('0');
    Serial.printf("%x", payload[i]);
  }
  if (payload.size() > n) Serial.print("...");
}
#endif

void onHidNotify(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool) {
  g_notifyReportId = 0;
  for (uint8_t i = 0; i < g_inputCharCount; ++i) {
    if (g_inputChars[i].chr == chr) g_notifyReportId = g_inputChars[i].reportId;
  }
  self().onReportIngest(data, len);
}

bool setupHid(NimBLEClient* client) {
  NimBLERemoteService* hid = client->getService(NimBLEUUID(kHidService));
  if (!hid) return false;

  // Prefer Report Protocol so we get full reports (and report ids) like a host.
  NimBLERemoteCharacteristic* proto = hid->getCharacteristic(NimBLEUUID(kCharProtocolMode));
  if (proto && proto->canWrite()) {
    uint8_t mode = 1;  // 1 = Report Protocol, 0 = Boot Protocol
    proto->writeValue(&mode, 1, false);
  }

  // Parse the HID Report Map for usage-page / byte-offset hints, so non-keyboard
  // page-turner remotes can be decoded by the generic fallback in onReportIngest.
  g_hasKeyboardPage = false;
  g_hasConsumerPage = false;
  g_preferredByteIndex = 0xFF;
  g_bareKeys = {};
  g_lastGenericCode = 0;
  if (NimBLERemoteCharacteristic* rmap = hid->getCharacteristic(NimBLEUUID(kCharReportMap))) {
    if (rmap->canRead()) {
      NimBLEAttValue v = rmap->readValue();
      parseReportMapHints(v.data(), v.size());
      g_bareKeys = bareKeysOf(v.data(), v.size());
#if FREEINK_BLE_HID_REPORT_DEBUG
      Serial.printf("[BleHid] report map: kbd=%d consumer=%d preferredByte=%d bareKeysLen=%u len=%u\n",
                    g_hasKeyboardPage, g_hasConsumerPage, (int)g_preferredByteIndex, (unsigned)g_bareKeys.len,
                    (unsigned)v.size());
#endif
    }
  }

  bool subscribed = false;
  g_inputCharCount = 0;
  const std::vector<NimBLERemoteCharacteristic*>& chars = hid->getCharacteristics(true);
  for (NimBLERemoteCharacteristic* c : chars) {
    if (!c) continue;
    if (c->getUUID() != NimBLEUUID(kCharReport) || !c->canNotify()) continue;
    // Report Reference descriptor (0x2908): byte[0] is the report id, byte[1] the
    // report type (1 = Input).
    bool isInput = true;
    uint8_t reportId = 0;
    NimBLERemoteDescriptor* ref = c->getDescriptor(NimBLEUUID(kDescReportReference));
    if (ref) {
      NimBLEAttValue v = ref->readValue();
      if (v.size() >= 2 && v[1] != 0x01) isInput = false;
      if (v.size() >= 1) reportId = v[0];
    }
    if (isInput && c->subscribe(true, onHidNotify)) {
      subscribed = true;
      if (g_inputCharCount < kMaxInputReportChars) g_inputChars[g_inputCharCount++] = {c, reportId};
    }
  }

  if (!subscribed) {  // fallback: boot keyboard input report
    // Boot reports always lead with the modifier byte. One byte store: a report
    // already notifying must never see a half-cleared layout.
    g_bareKeys.len = 0;
    NimBLERemoteCharacteristic* boot = hid->getCharacteristic(NimBLEUUID(kCharBootKbdInput));
    if (boot && boot->canNotify() && boot->subscribe(true, onHidNotify)) subscribed = true;
  }
  return subscribed;
}

bool hasHidService(NimBLEClient* client) {
  if (!client) return false;
  const NimBLEUUID hid(kHidService);
  if (client->getService(hid)) return true;
  // Some peripherals (Flic) return nothing for a search for one service UUID (ATT
  // Find By Type Value). Android and BlueZ list every service instead; do that too.
  // setupHid's getService() then finds HID in that list.
  for (const NimBLERemoteService* svc : client->getServices(true)) {
    if (svc->getUUID() == hid) return true;
  }
  return false;
}

// Ends an in-flight connection attempt at whatever stage it has reached.
// cancelConnect() stops only the GAP connect. Once the link is up the worker
// waits inside NimBLE for pairing or GATT discovery, and only a disconnect ends
// that wait. Deleting the worker there leaves NimBLE holding its task handle.
void cancelPendingConnect() {
  if (!g_client) return;
  g_client->cancelConnect();
  if (g_client->isConnected()) g_client->disconnect();
}

void doConnect(const char* addrStr, uint8_t type) {
  if (!g_client) {
    self().onConnectFailed("BLE client unavailable");
    return;
  }
  NimBLEDevice::getScan()->stop();
  NimBLEAddress addr(std::string(addrStr), type);
  g_disconnectObserved.store(false);
  if (!g_client->connect(addr)) {
    g_disconnectObserved.store(true);
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] connect failed: %s type=%u err=%d\n", addrStr, type, g_client->getLastError());
#endif
    if (!g_targetTryAltType) {
      self().onConnectFailed(g_client->getLastError() == BLE_HS_ETIMEOUT ? "Connect timeout" : "Connection failed");
      return;
    }
    type = type == 0 ? 1 : 0;
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] retry connect: %s type=%u\n", addrStr, type);
#endif
    addr = NimBLEAddress(std::string(addrStr), type);
    g_disconnectObserved.store(false);
    if (!g_client->connect(addr)) {
      g_disconnectObserved.store(true);
#if FREEINK_BLE_HID_SCAN_DEBUG
      Serial.printf("[BleHid] connect failed: %s type=%u err=%d\n", addrStr, type, g_client->getLastError());
#endif
      self().onConnectFailed(g_client->getLastError() == BLE_HS_ETIMEOUT ? "Connect timeout" : "Connection failed");
      return;
    }
  }
  if (!hasHidService(g_client)) {
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] no HID service: %s\n", addrStr);
#endif
    g_client->disconnect();
    self().onConnectFailed("Not a HID device");
    return;
  }
  if (!g_client->secureConnection()) {
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] security failed: %s err=%d\n", addrStr, g_client->getLastError());
#endif
    g_client->disconnect();
    self().onConnectFailed("Pairing failed");
    return;
  }
  if (!setupHid(g_client)) {
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] HID setup failed: %s\n", addrStr);
#endif
    g_client->disconnect();
    self().onConnectFailed("No HID input report");
    return;
  }
  self().onLinkUp(addrStr, nullptr, type);  // name resolved from scan/bond lists
}

void connTaskFn(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    doConnect(g_targetAddr, g_targetType);
    g_connecting = false;
  }
}

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (!dev) return;
    // Store named/HID advertisers by default; optionally keep anonymous
    // non-HID probe candidates during bring-up. HID is still validated at
    // connect time. The name falls back to the address. Keep the callback cheap
    // so heavy logging can't choke the C3's advertisement-report queue.
    const std::string a = dev->getAddress().toString();
    const bool named = dev->haveName();
    const std::string nm = named ? dev->getName() : a;
    const uint8_t type = dev->getAddress().getType();
    const int rssi = dev->getRSSI();
    const uint16_t appearance = dev->haveAppearance() ? dev->getAppearance() : 0;
    const bool keyboardAppearance = appearance == kAppearanceKeyboard;
    const bool connectable = dev->isConnectable();
    const bool hid = dev->isAdvertisingService(NimBLEUUID(kHidService)) || keyboardAppearance;
#if FREEINK_BLE_HID_SCAN_DEBUG
    if (named || hid || connectable) {
      Serial.printf("[BLE adv] %s  name='%s'  rssi=%d  hid=%d  app=0x%04x  conn=%d  addrType=%u",
                    a.c_str(), nm.c_str(), rssi, hid ? 1 : 0, appearance, connectable ? 1 : 0, type);
#if CONFIG_BT_NIMBLE_EXT_ADV
      Serial.printf("  legacy=%d  advType=0x%02x  data=%u  phy=%u/%u  len=%u", dev->isLegacyAdvertisement() ? 1 : 0,
                    dev->getAdvType(), dev->getDataStatus(), dev->getPrimaryPhy(), dev->getSecondaryPhy(),
                    dev->getAdvLength());
#else
      Serial.printf("  advType=0x%02x  len=%u", dev->getAdvType(), dev->getAdvLength());
#endif
      printPayloadHex(dev);
      Serial.println();
    }
#endif
    self().onScanResultIngest(a.c_str(), nm.c_str(), rssi, type, hid, connectable);
  }
};

class ClientCB : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*, int) override {
    g_disconnectObserved.store(true);
    self().onLinkDown();
  }
  void onPassKeyEntry(NimBLEConnInfo& connInfo) override { NimBLEDevice::injectPassKey(connInfo, 123456); }
  uint32_t onPassKeyDisplay(NimBLEConnInfo&) override {
    const uint32_t passkey = NimBLEDevice::getSecurityPasskey();
    self().onPairingPasskey(passkey);
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] pairing passkey: %06lu\n", static_cast<unsigned long>(passkey));
#endif
    return passkey;
  }
  void onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t) override {
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
  }
  // Reject peripheral connection-parameter updates — some keyboards request one
  // on the first keypress and drop the link if it's negotiated.
  bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params*) override { return false; }
};

ScanCB g_scanCb;
ClientCB g_clientCb;

}  // namespace

// --- Lifecycle ---------------------------------------------------------------
bool BleKeyboardHost::begin(const char* hostName) {
  if (begun_) return true;
  // An unfinished end() left the connection task, the client or the stack in
  // place: re-initializing now would pull NimBLE out from under them.
  if (g_teardownPending.load()) return false;
  g_autoReconnect = true;
  portENTER_CRITICAL(&g_mux);
  clearSelectedPlanLocked();
  portEXIT_CRITICAL(&g_mux);

#if FREEINK_BLE_HID_SCAN_DEBUG
  Serial.printf("[BleHid] begin: host='%s' bonds=%u\n", hostName ? hostName : "FreeInk", bondCount_);
#endif

  // Self-heal a partial teardown: if NimBLE still reports initialized (a previous
  // deinit raced and didn't complete), NimBLEDevice::init() would no-op and hand back
  // a dead stack. Force a clean deinit first so the init below actually runs.
  if (NimBLEDevice::isInitialized()) {
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.println("[BleHid] begin: NimBLE was already initialized; forcing deinit");
#endif
    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  loadBonds();
#if FREEINK_BLE_HID_SCAN_DEBUG
  Serial.printf("[BleHid] begin: loaded bonds=%u\n", bondCount_);
#endif
  if (!NimBLEDevice::init(hostName ? hostName : "FreeInk")) {
    Serial.println("[BleHid] begin: NimBLEDevice::init() failed");
    return false;
  }

  // HID page-turner reports are a few bytes; request the minimum ATT MTU so the
  // host doesn't reserve large per-connection GATT buffers. On RAM-constrained
  // targets (ESP32-C3, ~16 KB max contiguous block once the stack is up) every KB
  // back to the heap matters for the app (e.g. EPUB inflate windows).
  NimBLEDevice::setMTU(23);

  // Bonding for HID remotes. Default to Just Works because page-turners commonly
  // have no input/display capability; mandatory MITM makes those devices reject
  // pairing. Firmware that specifically needs host-display keyboard pairing can
  // opt in with FREEINK_BLE_HID_REQUIRE_MITM=1.
  NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/FREEINK_BLE_HID_REQUIRE_MITM, /*sc=*/false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(123456);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC);

  NimBLEScan* scan = NimBLEDevice::getScan();
  // wantDuplicates = true: DON'T filter duplicates. A device's name/HID often
  // rides in the scan response (a separate PDU after the first advertisement);
  // with duplicate filtering on, the C3 controller drops that follow-up, so the
  // device shows up nameless or not at all. This (plus the windowed interval
  // below and the no-filter onResult) keeps scan response and extended adv data.
  scan->setScanCallbacks(&g_scanCb, true);
  // onResult() copies what the UI needs into devices_, and nothing reads NimBLE's
  // own result list. Callback-only mode frees each advertiser once it has been
  // reported instead of keeping every one heard on the heap until the next scan.
  scan->setMaxResults(0);
  scan->setActiveScan(true);  // send scan requests -> receive scan responses (names)
  // CONTINUOUS listening (window == interval, 100% duty; values are ms).
  // Extended advertising splits data into an AUX packet on a secondary
  // channel that the controller must catch at a precise moment after the primary
  // — if the scan window is closed when it lands, the name/HID UUID is lost. A
  // windowed (low-duty) scan is fine for legacy keyboards but starves AUX
  // reception, which is the only data this keyboard exposes. Duplicate filtering
  // is OFF (above) so the AUX packet (same address as the primary) isn't dropped.
  scan->setInterval(160);
  scan->setWindow(160);
#if CONFIG_BT_NIMBLE_EXT_ADV
  // Some BLE 5.x peripherals advertise on LE Coded. Scan both PHYs so the pairing
  // UI sees the same devices a desktop Bluetooth stack reports.
  scan->setPhy(NimBLEScan::SCAN_ALL);
#endif

  g_client = NimBLEDevice::createClient();
  if (!g_client) {
    // NimBLE can refuse a new client if a previous one wasn't reclaimed (e.g. rapid
    // deinit/init cycles). Don't dereference null — unwind cleanly so a later begin()
    // can retry from a clean state.
    Serial.println("[BleHid] begin: createClient() returned null");
    NimBLEDevice::deinit(true);
    return false;
  }
  g_client->setConnectTimeout(kConnectTimeoutMs);
  // Widen the link supervision timeout. The host runs on a single RISC-V core and
  // some operations block it for several seconds (e.g. the reader rebuilding a whole
  // chapter to reach its last page on a backward page turn across a section boundary).
  // NimBLE's default supervision timeout is only 2.56s, so those long renders overrun
  // it and the controller drops the link — the peripheral then fails to re-encrypt on
  // auto-reconnect (HCI 0x08 "Connection Timeout" -> BLE_HS err 520). Request a low
  // interval for keypress responsiveness with an 8s supervision timeout (units:
  // interval 1.25ms, timeout 10ms) so a slow page render can't sever the connection.
  // Peripheral-initiated updates are still rejected (see onConnParamsUpdateRequest), so
  // these host-chosen params hold for the life of the link.
  g_client->setConnectionParams(/*minInterval=*/12, /*maxInterval=*/24, /*latency=*/0, /*timeout=*/800);
  g_client->setClientCallbacks(&g_clientCb, false);

  xTaskCreate(connTaskFn, "ble-conn", 4096, nullptr, 3, &g_connTask);
  begun_ = true;
#if FREEINK_BLE_HID_SCAN_DEBUG
  Serial.println("[BleHid] begin: ok");
#endif
  return true;
}

bool BleKeyboardHost::end(uint32_t timeoutMs) {
  if (g_endBusy.exchange(true)) return false;
  struct EndBusyRelease {
    ~EndBusyRelease() { g_endBusy.store(false); }
  } endBusyRelease;
  // Read under g_endBusy: no other end() can be between its begun_ and
  // g_teardownPending writes, so "both false" really means stopped.
  if (!begun_ && !g_teardownPending.load()) return true;
  const uint32_t budgetMs = timeoutMs > kMaxTeardownTimeoutMs ? kMaxTeardownTimeoutMs : timeoutMs;
  const uint32_t startMs = millis();
  portENTER_CRITICAL(&g_mux);
  clearSelectedPlanLocked();
  portEXIT_CRITICAL(&g_mux);
  if (begun_) {
    // Stopping before not-running: an observer of isRunning()/isStopping() never
    // sees both false while the stack is still up.
    g_teardownPending.store(true);
    begun_ = false;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();
  }

  // If the connection task is inside a connect, pairing or GATT wait, do not
  // delete it or deinit NimBLE under it: killing it there leaves NimBLE holding a
  // dead task and can crash on Bluetooth-off, sleep, or the next begin(). Cancel
  // on every pass: the task can bring the link up after the first cancel.
  while (g_connecting) {
    cancelPendingConnect();
    if (millis() - startMs >= budgetMs) return false;
    vTaskDelay(pdMS_TO_TICKS(kTeardownPollMs));
  }
  if (g_connTask) {
    vTaskDelete(g_connTask);
    g_connTask = nullptr;
  }

  // Close the link and delete the client BEFORE deinit. NimBLE keeps a fixed-size
  // client array that survives deinit/init, and deleteClient() only DEFERS the
  // delete while the client is CONNECTED/DISCONNECTING. deinit() would then tear
  // the host down before that deferred delete runs, the slot leaks, and the next
  // begin()'s createClient() returns null for good.
  if (g_client) {
    while (!clientReleased()) {
      if (g_client->isConnected()) g_client->disconnect();
      if (millis() - startMs >= budgetMs) return false;
      vTaskDelay(pdMS_TO_TICKS(kTeardownPollMs));
    }
    NimBLEDevice::deleteClient(g_client);
    g_client = nullptr;
  }

  // Free the NimBLE host + BT controller memory back to the heap. Bonds live in
  // NVS and survive this. A deinit that did not complete is retried by the next
  // end(), so the next begin() never inherits a half-initialized stack.
  NimBLEDevice::deinit(true);
  if (NimBLEDevice::isInitialized()) return false;
  g_lastGenericCode = 0;
  g_lastReportMs = 0;

  portENTER_CRITICAL(&g_mux);
  connected_ = false;
  connecting_ = false;
  scanning_ = false;
  deviceCount_ = 0;
  ringHead_ = 0;
  ringTail_ = 0;
  heldUsage_ = 0;
  clearSelectedPlanLocked();
  rawHead_ = 0;
  rawTail_ = 0;
  rawCode_ = 0;
  rawReports_ = 0;
  rawDropped_ = false;
  portEXIT_CRITICAL(&g_mux);
  rawRestCount_ = 0;
  connAddr_[0] = '\0';
  g_teardownPending.store(false);
  return true;
}

bool BleKeyboardHost::isStopping() const { return g_teardownPending.load(); }

void BleKeyboardHost::poll() {
  if (!begun_) return;

  // Reflect the live scanner state.
  scanning_ = NimBLEDevice::getScan()->isScanning();

#include "HidHostPoll.inc"
}

// --- Discovery ---------------------------------------------------------------
void BleKeyboardHost::startScan(uint32_t ms) {
  if (!begun_) {
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.println("[BleHid] scan start ignored: host not begun");
#endif
    return;
  }
  if (g_connecting && g_client) {
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.println("[BleHid] scan start: cancelling pending reconnect");
#endif
    const uint32_t waitStart = millis();
    while (g_connecting && millis() - waitStart < kTeardownConnectWaitMs) {
      cancelPendingConnect();
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
  portENTER_CRITICAL(&g_mux);
  deviceCount_ = 0;
  portEXIT_CRITICAL(&g_mux);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->clearResults();
#if FREEINK_BLE_HID_SCAN_DEBUG
  Serial.printf("[BleHid] scan start: active, continuous, %lu ms wasScanning=%d\n", static_cast<unsigned long>(ms),
                scan->isScanning() ? 1 : 0);
#endif
  const bool started = scan->start(ms, false, true);
  scanning_ = scan->isScanning();
#if FREEINK_BLE_HID_SCAN_DEBUG
  Serial.printf("[BleHid] scan start result: started=%d scanning=%d\n", started ? 1 : 0, scanning_ ? 1 : 0);
#endif
}

void BleKeyboardHost::stopScan() {
  if (!begun_) return;
#if FREEINK_BLE_HID_SCAN_DEBUG
  Serial.printf("[BleHid] scan stop: wasScanning=%d devices=%u\n", NimBLEDevice::getScan()->isScanning() ? 1 : 0,
                deviceCount_);
#endif
  NimBLEDevice::getScan()->stop();
  scanning_ = false;
}

const DiscoveredDevice& BleKeyboardHost::device(uint8_t i) const {
  static const DiscoveredDevice kEmpty{};
  return i < deviceCount_ ? devices_[i] : kEmpty;
}

void BleKeyboardHost::releaseScanResults() {
  if (begun_) NimBLEDevice::getScan()->clearResults();
  portENTER_CRITICAL(&g_mux);
  deviceCount_ = 0;
  portEXIT_CRITICAL(&g_mux);
}

// --- Connection --------------------------------------------------------------
bool BleKeyboardHost::connect(const char* addr) { return connectInternal(addr, /*explicitRequest=*/true); }

bool BleKeyboardHost::armSelectedPeerReconnect(const char* addr) {
  if (!addr || strnlen(addr, sizeof(g_selectedAddr)) != sizeof(g_selectedAddr) - 1) return false;
  if (!begun_ || !g_connTask || connected_ || g_connecting || scanning_) return false;
  bool bonded = false;
  for (uint8_t i = 0; i < bondCount_; ++i) {
    if (strncmp(bonds_[i].addr, addr, sizeof(bonds_[i].addr)) == 0) bonded = true;
  }
  if (!bonded) return false;
  portENTER_CRITICAL(&g_mux);
  const bool armed = g_selectedAddr[0] == '\0';
  if (armed) {
    memcpy(g_selectedAddr, addr, sizeof(g_selectedAddr));
    g_selectedAttemptsLeft = kSelectedReconnectAttempts;
    g_selectedStartedMs = millis();
    g_lastReconnectMs = g_selectedStartedMs - kReconnectBackoffMs;  // the first poll tries at once
  }
  portEXIT_CRITICAL(&g_mux);
  return armed;
}

bool BleKeyboardHost::connectInternal(const char* addr, const bool explicitRequest) {
  if (!begun_ || !addr || g_connecting) return false;
  if (explicitRequest) {
    portENTER_CRITICAL(&g_mux);
    clearSelectedPlanLocked();
    g_autoReconnect = true;
    portEXIT_CRITICAL(&g_mux);
  }

  uint8_t type = 0;
  bool knownType = false;
  for (uint8_t i = 0; i < deviceCount_; ++i) {
    if (strncmp(devices_[i].addr, addr, sizeof(devices_[i].addr)) == 0) {
      type = devices_[i].addrType;
      knownType = true;
      break;
    }
  }
  for (uint8_t i = 0; i < bondCount_; ++i) {
    if (strncmp(bonds_[i].addr, addr, sizeof(bonds_[i].addr)) == 0) {
      type = bonds_[i].addrType;
      knownType = true;
      break;
    }
  }

  strncpy(g_targetAddr, addr, sizeof(g_targetAddr) - 1);
  g_targetAddr[sizeof(g_targetAddr) - 1] = '\0';
  g_targetType = type;
  g_targetTryAltType = !knownType;
  g_connecting = true;
  connecting_ = true;
  connectFailed_ = false;
  pairingPasskeyReady_ = false;
  connectFailure_[0] = '\0';
  if (g_connTask) xTaskNotifyGive(g_connTask);
  return true;
}

void BleKeyboardHost::disconnect() {
  g_autoReconnect = false;
  portENTER_CRITICAL(&g_mux);
  clearSelectedPlanLocked();
  portEXIT_CRITICAL(&g_mux);
  if (g_client && g_client->isConnected()) g_client->disconnect();
}

// --- Pairings ----------------------------------------------------------------
const PairedHidDevice& BleKeyboardHost::paired(uint8_t i) const {
  static const PairedHidDevice kEmpty{};
  return i < bondCount_ ? bonds_[i] : kEmpty;
}

void BleKeyboardHost::forget(const char* addr) {
  if (!addr) return;
  for (uint8_t i = 0; i < bondCount_; ++i) {
    if (strncmp(bonds_[i].addr, addr, sizeof(bonds_[i].addr)) != 0) continue;
    NimBLEDevice::deleteBond(NimBLEAddress(std::string(bonds_[i].addr), bonds_[i].addrType));
    portENTER_CRITICAL(&g_mux);
    if (strncmp(g_selectedAddr, addr, sizeof(g_selectedAddr)) == 0) clearSelectedPlanLocked();
    portEXIT_CRITICAL(&g_mux);
    for (uint8_t j = i + 1; j < bondCount_; ++j) bonds_[j - 1] = bonds_[j];
    bondCount_--;
    persistBonds();
    return;
  }
}

#include "HidHostCommon.inc"

}  // namespace freeink

#elif !FREEINK_CAP_BLE_HID_HOST  // !FREEINK_CAP_BLE_HID_HOST — stub bodies, no BLE code linked.

namespace freeink {

bool BleKeyboardHost::begin(const char*) { return false; }
bool BleKeyboardHost::end(uint32_t) { return true; }
bool BleKeyboardHost::isStopping() const { return false; }
void BleKeyboardHost::poll() {}
void BleKeyboardHost::startScan(uint32_t) {}
void BleKeyboardHost::stopScan() {}
const DiscoveredDevice& BleKeyboardHost::device(uint8_t) const {
  static const DiscoveredDevice kEmpty{};
  return kEmpty;
}
void BleKeyboardHost::releaseScanResults() {}
bool BleKeyboardHost::connect(const char*) { return false; }
bool BleKeyboardHost::armSelectedPeerReconnect(const char*) { return false; }
void BleKeyboardHost::disconnect() {}
const PairedHidDevice& BleKeyboardHost::paired(uint8_t) const {
  static const PairedHidDevice kEmpty{};
  return kEmpty;
}
void BleKeyboardHost::forget(const char*) {}
bool BleKeyboardHost::popKey(KeyEvent&) { return false; }
bool BleKeyboardHost::popRawButton(RawButtonEvent&) { return false; }
void BleKeyboardHost::onScanResultIngest(const char*, const char*, int, uint8_t, bool, bool) {}
void BleKeyboardHost::onReportIngest(const uint8_t*, size_t) {}
void BleKeyboardHost::onLinkUp(const char*, const char*, uint8_t) {}
void BleKeyboardHost::onLinkDown() {}
void BleKeyboardHost::onConnectFailed(const char*) {}
bool BleKeyboardHost::takeConnectFailure(char*, size_t) { return false; }
void BleKeyboardHost::onPairingPasskey(uint32_t) {}
bool BleKeyboardHost::takePairingPasskey(uint32_t&) { return false; }
void BleKeyboardHost::enqueue(const KeyEvent&) {}
void BleKeyboardHost::emitUsage(uint8_t, uint8_t) {}
void BleKeyboardHost::persistBonds() {}
void BleKeyboardHost::loadBonds() {}

}  // namespace freeink

#endif  // FREEINK_CAP_BLE_HID_HOST
