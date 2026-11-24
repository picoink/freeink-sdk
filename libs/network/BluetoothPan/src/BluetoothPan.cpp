#include <BluetoothPan.h>

#include "PanProtocol.h"

using Pan = freeink::BluetoothPan;
Pan& Pan::instance() {
  static Pan value;
  return value;
}
const char* Pan::stateName(State state) {
  static const char* names[] = {"off",          "starting",   "idle",
                                "scanning",     "connecting", "pairing",
                                "obtaining_ip", "connected",  "retry_wait",
                                "stopping",     "error"};
  const auto n = static_cast<unsigned>(state);
  return n < sizeof(names) / sizeof(*names) ? names[n] : "error";
}

#if FREEINK_CAP_BLUETOOTH_PAN && !defined(FREEINK_HOST) && \
    defined(ARDUINO_ARCH_ESP32)
static_assert(Pan::supported(),
              "Bluetooth PAN enabled but Classic/Bluedroid/PAN is missing; "
              "build the FreeInk PAN libraries");
#include <Arduino.h>
#include <Network.h>
#include <Preferences.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <BluetoothHost.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_netif.h>
#include <esp_netif_defaults.h>
#include <esp_pan_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace freeink::pan_detail;
enum Kind : uint8_t {
  Scan,
  Connect,
  Disconnect,
  Confirm,
  Forget,
  Ready,
  Deinitialized,
  Open,
  Closed,
  Found,
  ScanDone,
  Auth,
  Pair,
  GotIp,
  LostIp
};
struct Event {
  Kind kind{};
  Pan::Device device{};
  uint32_t value = 0;
  uint16_t handle = ESP_PAN_INVALID_HANDLE;
  bool ok = false;
  char ip[16]{};
};
struct Packet {
  uint8_t* data;
  size_t size;
  uint16_t handle;
};
struct Context {
  portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  Pan::Status status;
  std::atomic<bool> running{false}, accepting{false}, congested{false},
      overflow{false}, stopRequested{false};
  std::atomic<uint16_t> handle{ESP_PAN_INVALID_HANDLE};
  std::atomic<uint32_t> drops{0};
  StaticQueue_t eventsStorage{}, packetsStorage{};
  uint8_t eventBytes[24 * sizeof(Event)]{}, packetBytes[8 * sizeof(Packet)]{};
  QueueHandle_t events =
      xQueueCreateStatic(24, sizeof(Event), eventBytes, &eventsStorage);
  QueueHandle_t packets =
      xQueueCreateStatic(8, sizeof(Packet), packetBytes, &packetsStorage);
  esp_netif_driver_base_t glue{};
  esp_netif_t* netif = nullptr;
  esp_event_handler_instance_t gotIp = nullptr, lostIp = nullptr;
  Preferences preferences;
  bool prefs = false, ready = false, stopping = false, deinitRequested = false,
       initRequested = false;
  const char* stopError = nullptr;
  bool scanning = false, connectAfterScan = false, finished = false;
  uint32_t since = 0;
  Retry retry;
  Pan::Device peer, savedPeer;
  // Publish/read only under the short snapshot lock. No SDK calls in this lock.
  void state(Pan::State state, const char* message) {
    portENTER_CRITICAL(&lock);
    status.state = state;
    status.enabled = state != Pan::State::Off;
    status.pairing = state == Pan::State::Pairing;
    snprintf(status.message, sizeof(status.message), "%s", message);
    ++status.revision;
    portEXIT_CRITICAL(&lock);
    since = millis();
  }
  void peerStatus() {
    portENTER_CRITICAL(&lock);
    status.peer = peer;
    ++status.revision;
    portEXIT_CRITICAL(&lock);
  }
  bool post(const Event& e) {
    if (!accepting.load()) return false;
    if (xQueueSend(events, &e, 0) == pdPASS) return true;
    if (e.kind != Found) overflow = true;
    return false;
  }
} context;
Pan::Status snapshot() {
  portENTER_CRITICAL(&context.lock);
  auto out = context.status;
  portEXIT_CRITICAL(&context.lock);
  out.droppedFrames = context.drops.load();
  return out;
}
void ipEvent(void*, esp_event_base_t, int32_t id, void* data) {
  if (!context.accepting.load()) return;
  Event e;
  if (id == IP_EVENT_ETH_GOT_IP) {
    const auto* ip = static_cast<ip_event_got_ip_t*>(data);
    if (!ip || ip->esp_netif != context.netif) return;
    e.kind = GotIp;
    snprintf(e.ip, sizeof(e.ip), IPSTR, IP2STR(&ip->ip_info.ip));
  } else {
    const auto* ip = static_cast<ip_event_got_ip_t*>(data);
    if (!ip || ip->esp_netif != context.netif) return;
    e.kind = LostIp;
  }
  e.handle = context.handle.load();
  context.post(e);
}
void gapEvent(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* p) {
  if (!context.accepting.load()) return;
  Event e;
  switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
      e.kind = Found;
      memcpy(e.device.address, p->disc_res.bda, 6);
      for (int i = 0; i < p->disc_res.num_prop; ++i) {
        const auto& prop = p->disc_res.prop[i];
        if (prop.type == ESP_BT_GAP_DEV_PROP_RSSI && prop.len >= 1)
          e.device.rssi = *static_cast<int8_t*>(prop.val);
        if (prop.type == ESP_BT_GAP_DEV_PROP_BDNAME && prop.len > 0)
          snprintf(e.device.name, sizeof(e.device.name), "%.*s", int(prop.len),
                   static_cast<char*>(prop.val));
        if (prop.type == ESP_BT_GAP_DEV_PROP_EIR && prop.val && prop.len > 0) {
          const auto* bytes = static_cast<const uint8_t*>(prop.val);
          for (size_t at = 0; at < size_t(prop.len) && bytes[at];) {
            const size_t length = bytes[at];
            if (length > size_t(prop.len) - at - 1) break;
            const auto type = bytes[at + 1];
            if (length > 1 && (type == ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME ||
                               type == ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME)) {
              snprintf(e.device.name, sizeof(e.device.name), "%.*s",
                       int(length - 1),
                       reinterpret_cast<const char*>(bytes + at + 2));
              if (type == ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME) break;
            }
            at += length + 1;
          }
        }
      }
      if (!e.device.name[0])
        snprintf(e.device.name, sizeof(e.device.name),
                 "%02X:%02X:%02X:%02X:%02X:%02X", e.device.address[0],
                 e.device.address[1], e.device.address[2], e.device.address[3],
                 e.device.address[4], e.device.address[5]);
      break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      if (p->disc_st_chg.state != ESP_BT_GAP_DISCOVERY_STOPPED) return;
      e.kind = ScanDone;
      break;
    case ESP_BT_GAP_CFM_REQ_EVT:
      e.kind = Pair;
      e.value = p->cfm_req.num_val;
      memcpy(e.device.address, p->cfm_req.bda, 6);
      break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
      e.kind = Auth;
      e.ok = p->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS;
      memcpy(e.device.address, p->auth_cmpl.bda, 6);
      break;
    case ESP_BT_GAP_PIN_REQ_EVT:
      // iPhone uses SSP numeric comparison. Never silently accept a legacy PIN.
      esp_bt_gap_pin_reply(p->pin_req.bda, false, 0, nullptr);
      return;
    default:
      return;
  }
  context.post(e);
}
void panEvent(esp_pan_cb_event_t event, esp_pan_cb_param_t* p) {
  if (!context.accepting.load()) return;
  Event e;
  switch (event) {
    case ESP_PAN_INIT_EVT:
      e.kind = Ready;
      e.ok = p->init.status == ESP_PAN_SUCCESS;
      break;
    case ESP_PAN_DEINIT_EVT:
      e.kind = Deinitialized;
      e.ok = p->deinit.status == ESP_PAN_SUCCESS ||
             p->deinit.status == ESP_PAN_NEED_INIT;
      break;
    case ESP_PAN_OPEN_EVT:
      e.kind = Open;
      e.ok = p->open.status == ESP_PAN_SUCCESS;
      e.handle = p->open.handle;
      memcpy(e.device.address, p->open.remote_bda, 6);
      break;
    case ESP_PAN_CLOSE_EVT:
      e.kind = Closed;
      e.handle = p->close.handle;
      break;
    case ESP_PAN_CONG_EVT:
      if (p->cong.handle == context.handle.load())
        context.congested = p->cong.cong;
      return;
    case ESP_PAN_DATA_IND_EVT: {
      auto& d = p->data_ind;
      if (d.handle != context.handle.load() || !d.data || !d.len ||
          d.len > Mtu || d.ext)
        return;
      Packet packet{static_cast<uint8_t*>(malloc(d.len + Header)),
                    d.len + Header, d.handle};
      if (!packet.data) {
        ++context.drops;
        return;
      }
      memcpy(packet.data, d.dst, 6);
      memcpy(packet.data + 6, d.src, 6);
      packet.data[12] = d.protocol >> 8;
      packet.data[13] = d.protocol;
      memcpy(packet.data + Header, d.data, d.len);
      if (xQueueSend(context.packets, &packet, 0) != pdPASS) {
        free(packet.data);
        ++context.drops;
      }
      return;
    }
    default:
      return;
  }
  context.post(e);
}
esp_err_t transmit(void*, void* data, size_t size) {
  auto* bytes = static_cast<uint8_t*>(data);
  auto handle = context.handle.load();
  if (!frame(bytes, size)) return ESP_ERR_INVALID_ARG;
  if (handle == ESP_PAN_INVALID_HANDLE || context.congested.load())
    return ESP_ERR_INVALID_STATE;
  return esp_pan_write(handle, bytes, bytes + 6,
                       (uint16_t(bytes[12]) << 8) | bytes[13], size - Header,
                       bytes + Header, false);
}
void freePacket(void*, void* data) { free(data); }
esp_err_t attach(esp_netif_t* netif, void*) {
  context.glue.netif = netif;
  esp_netif_driver_ifconfig_t cfg{};
  cfg.handle = &context;
  cfg.transmit = transmit;
  cfg.driver_free_rx_buffer = freePacket;
  auto result = esp_netif_set_driver_config(netif, &cfg);
  if (result != ESP_OK) return result;
  return esp_netif_set_mac(netif,
                           const_cast<uint8_t*>(esp_bt_dev_get_address()));
}
void linkDown() {
  context.handle = ESP_PAN_INVALID_HANDLE;
  context.congested = false;
  if (context.netif) {
    esp_netif_action_disconnected(context.netif, nullptr, 0, nullptr);
    esp_netif_action_stop(context.netif, nullptr, 0, nullptr);
  }
  portENTER_CRITICAL(&context.lock);
  context.status.ip[0] = 0;
  ++context.status.revision;
  portEXIT_CRITICAL(&context.lock);
}
void retry(const char* message) {
  linkDown();
  context.retry.schedule(millis());
  context.state(
      context.retry.enabled ? Pan::State::RetryWait : Pan::State::Idle,
      message);
}
void connectNow() {
  if (context.stopping || !context.ready || !addressValid(context.peer.address))
    return;
  context.peerStatus();
  context.state(Pan::State::Connecting,
                "Connecting to shared Bluetooth network");
  if (esp_pan_connect(context.peer.address, ESP_PAN_ROLE_PANU,
                      ESP_PAN_ROLE_NAP) != ESP_OK)
    retry("Connection failed; retrying");
}
void stop(const char* error = nullptr) {
  if (error) context.stopError = error;
  if (!context.stopping) {
    if (snapshot().pairing)
      esp_bt_gap_ssp_confirm_reply(context.peer.address, false);
    context.stopping = true;
    context.retry.enabled = false;
    context.connectAfterScan = false;
    if (context.scanning) esp_bt_gap_cancel_discovery();
    linkDown();
    context.state(Pan::State::Stopping, "Stopping Bluetooth network");
  }
  if (context.initRequested && !context.deinitRequested) {
    context.deinitRequested = esp_pan_deinit() == ESP_OK;
  }
}
void handle(const Event& e) {
  auto s = snapshot();
  if (context.stopping && e.kind != Deinitialized && e.kind != Closed &&
      e.kind != Ready && e.kind != Open)
    return;
  switch (e.kind) {
    case Ready:
      if (!e.ok) {
        context.state(Pan::State::Error, "PAN initialization failed");
        context.finished = true;
        break;
      }
      context.ready = true;
      if (context.stopping)
        stop();
      else {
        context.state(
            Pan::State::Idle,
            "Enable iPhone Personal Hotspot, then open Bluetooth settings");
        if (s.saved) {
          context.retry.enabled = true;
          connectNow();
        }
      }
      break;
    case Deinitialized:
      if (e.ok) {
        context.ready = false;
        context.finished = true;
      } else {
        context.deinitRequested = false;
        context.state(Pan::State::Stopping, "PAN cleanup retrying");
      }
      break;
    case Scan:
      if (!context.ready || context.scanning ||
          context.handle != ESP_PAN_INVALID_HANDLE || s.pairing ||
          s.state == Pan::State::Connecting)
        break;
      context.retry.enabled = false;
      portENTER_CRITICAL(&context.lock);
      context.status.count = 0;
      portEXIT_CRITICAL(&context.lock);
      context.scanning = esp_bt_gap_start_discovery(
                             ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0) == ESP_OK;
      context.state(context.scanning ? Pan::State::Scanning : Pan::State::Error,
                    context.scanning
                        ? "Scanning; keep iPhone Bluetooth settings open"
                        : "Scan could not start");
      break;
    case Found:
      if (!context.scanning) break;
      portENTER_CRITICAL(&context.lock);
      {
        size_t i = 0;
        while (i < context.status.count &&
               memcmp(context.status.devices[i].address, e.device.address, 6))
          ++i;
        if (i < Pan::MaxDevices) {
          context.status.devices[i] = e.device;
          if (i == context.status.count) ++context.status.count;
          ++context.status.revision;
        }
      }
      portEXIT_CRITICAL(&context.lock);
      break;
    case ScanDone:
      context.scanning = false;
      if (context.connectAfterScan) {
        context.connectAfterScan = false;
        connectNow();
      } else if (s.state == Pan::State::Scanning)
        context.state(Pan::State::Idle, "Select a device to connect");
      break;
    case Connect:
      if (!context.ready || context.connectAfterScan || s.pairing ||
          s.state == Pan::State::Connecting ||
          context.handle != ESP_PAN_INVALID_HANDLE)
        break;
      context.peer = e.device;
      portENTER_CRITICAL(&context.lock);
      context.status.saved =
          !memcmp(context.peer.address, context.savedPeer.address, 6);
      portEXIT_CRITICAL(&context.lock);
      context.retry.enabled = true;
      context.retry.attempts = 0;
      if (context.scanning) {
        context.connectAfterScan = true;
        esp_bt_gap_cancel_discovery();
      } else
        connectNow();
      break;
    case Pair:
      if (memcmp(e.device.address, context.peer.address, 6) ||
          s.state != Pan::State::Connecting) {
        auto copy = e.device;
        esp_bt_gap_ssp_confirm_reply(copy.address, false);
        break;
      }
      portENTER_CRITICAL(&context.lock);
      context.status.pairingCode = e.value;
      portEXIT_CRITICAL(&context.lock);
      context.state(Pan::State::Pairing,
                    "Compare this code with iPhone; confirm on both devices");
      break;
    case Confirm:
      if (!s.pairing) break;
      if (!e.ok)
        stop("Pairing rejected; enable Bluetooth to retry");
      else {
        esp_bt_gap_ssp_confirm_reply(context.peer.address, true);
        context.state(Pan::State::Connecting,
                      "Waiting for iPhone pairing approval");
      }
      break;
    case Auth:
      if (!memcmp(e.device.address, context.peer.address, 6) && !e.ok) {
        stop("Pairing failed; forget on iPhone and enable Bluetooth to retry");
      }
      break;
    case Open:
      if (e.ok && (context.stopping || !context.retry.enabled ||
                   memcmp(e.device.address, context.peer.address, 6))) {
        esp_pan_disconnect(e.handle);
        break;
      }
      if (!e.ok) {
        if (!context.stopping && context.retry.enabled &&
            !memcmp(e.device.address, context.peer.address, 6))
          retry("PAN connection failed; check Personal Hotspot");
        break;
      }
      context.handle = e.handle;
      context.congested = false;
      esp_netif_set_default_netif(context.netif);
      esp_netif_action_start(context.netif, nullptr, 0, nullptr);
      esp_netif_action_connected(context.netif, nullptr, 0, nullptr);
      context.state(Pan::State::ObtainingIp,
                    "Bluetooth connected; requesting IP address");
      break;
    case GotIp:
      if (e.handle == ESP_PAN_INVALID_HANDLE || e.handle != context.handle ||
          !context.retry.enabled)
        break;
      portENTER_CRITICAL(&context.lock);
      snprintf(context.status.ip, sizeof(context.status.ip), "%s", e.ip);
      ++context.status.connection;
      portEXIT_CRITICAL(&context.lock);
      context.retry.attempts = 0;
      context.state(Pan::State::Connected,
                    "Bluetooth network has an IP address");
      if (context.prefs &&
          context.preferences.putBytes("peer", context.peer.address, 6) == 6) {
        context.preferences.putString("name", context.peer.name);
        context.savedPeer = context.peer;
        portENTER_CRITICAL(&context.lock);
        context.status.saved = true;
        portEXIT_CRITICAL(&context.lock);
      }
      break;
    case LostIp:
      if (e.handle == context.handle && e.handle != ESP_PAN_INVALID_HANDLE)
        esp_pan_disconnect(e.handle);
      break;
    case Closed:
      if (e.handle != context.handle) break;
      if (context.stopping)
        linkDown();
      else
        retry(context.retry.enabled ? "Link lost; reconnecting"
                                    : "Disconnected");
      break;
    case Disconnect:
    case Forget:
      context.retry.enabled = false;
      context.connectAfterScan = false;
      if (s.pairing) esp_bt_gap_ssp_confirm_reply(context.peer.address, false);
      if (context.scanning) esp_bt_gap_cancel_discovery();
      if (e.kind == Forget) {
        if (addressValid(context.peer.address))
          esp_bt_gap_remove_bond_device(context.peer.address);
        if (addressValid(context.savedPeer.address) &&
            memcmp(context.savedPeer.address, context.peer.address, 6))
          esp_bt_gap_remove_bond_device(context.savedPeer.address);
        context.savedPeer = {};
        if (context.prefs) {
          context.preferences.remove("peer");
          context.preferences.remove("name");
        }
        portENTER_CRITICAL(&context.lock);
        context.status.saved = false;
        portEXIT_CRITICAL(&context.lock);
      }
      // PAN has no connect-cancel operation. Drain the whole profile so a late
      // OPEN cannot attach to a new attempt (or re-save a forgotten device).
      stop();
      break;
  }
}
bool initialize() {
  if (!Network.begin() || !freeink::BluetoothHost::acquire(freeink::BluetoothHost::Profile::Pan)) return false;
  esp_bt_io_cap_t io = ESP_BT_IO_CAP_IO;
  if (esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io, sizeof(io)) !=
          ESP_OK ||
      esp_bt_gap_register_callback(gapEvent) != ESP_OK ||
      esp_pan_register_callback(panEvent) != ESP_OK)
    return false;
  esp_bt_gap_set_device_name("PicoInk");
  // Only pair with a user-selected peer; no incoming, unsolicited pairing.
  esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
  base.if_key = "FREEINK_PAN";
  base.if_desc = "pan";
  base.route_prio = 60;
  esp_netif_config_t cfg{};
  cfg.base = &base;
  cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  context.netif = esp_netif_new(&cfg);
  context.glue.post_attach = attach;
  if (!context.netif ||
      esp_netif_attach(context.netif, &context.glue) != ESP_OK)
    return false;
  if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                          ipEvent, nullptr,
                                          &context.gotIp) != ESP_OK ||
      esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_LOST_IP,
                                          ipEvent, nullptr,
                                          &context.lostIp) != ESP_OK)
    return false;
  esp_pan_cfg_t pan = ESP_PAN_DEFAULT_CONFIG();
  context.initRequested = esp_pan_init(&pan) == ESP_OK;
  return context.initRequested;
}
bool cleanup() {
  context.accepting = false;
  linkDown();
  if (context.gotIp)
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                          context.gotIp);
  if (context.lostIp)
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_LOST_IP,
                                          context.lostIp);
  context.gotIp = context.lostIp = nullptr;
  if (!freeink::BluetoothHost::release(freeink::BluetoothHost::Profile::Pan)) {
    context.state(Pan::State::Stopping, "Bluetooth cleanup failed; retrying");
    return false;  // Never advertise a stopped radio while the controller is
                   // active.
  }
  Packet packet;
  while (xQueueReceive(context.packets, &packet, 0) == pdPASS)
    free(packet.data);
  if (context.netif) esp_netif_destroy(context.netif);
  context.netif = nullptr;
  if (context.prefs) context.preferences.end();
  context.prefs = false;
  if (context.stopError)
    context.state(Pan::State::Error, context.stopError);
  else if (context.stopping)
    context.state(Pan::State::Off, "Bluetooth network off");
  portENTER_CRITICAL(&context.lock);
  context.status.enabled = false;
  portEXIT_CRITICAL(&context.lock);
  return true;
}
void worker(void*) {
  context.prefs = context.preferences.begin("freeink-pan", false);
  context.peer = {};
  const bool saved =
      context.prefs && context.preferences.getBytesLength("peer") == 6 &&
      context.preferences.getBytes("peer", context.peer.address, 6) == 6 &&
      addressValid(context.peer.address);
  if (saved)
    context.preferences.getString("name", context.peer.name,
                                  sizeof(context.peer.name));
  context.savedPeer = saved ? context.peer : Pan::Device{};
  portENTER_CRITICAL(&context.lock);
  context.status.saved = saved;
  portEXIT_CRITICAL(&context.lock);
  context.peerStatus();
  if (!initialize()) {
    context.state(Pan::State::Error, "Bluetooth initialization failed");
    context.finished = true;
  }
  while (!context.finished) {
    if (context.stopRequested.exchange(false)) stop();
    Event event;
    if (xQueueReceive(context.events, &event, pdMS_TO_TICKS(20)) == pdPASS)
      handle(event);
    Packet packet;
    for (unsigned n = 0;
         n < 8 && xQueueReceive(context.packets, &packet, 0) == pdPASS; ++n) {
      if (!context.stopping && packet.handle == context.handle && context.netif)
        esp_netif_receive(context.netif, packet.data, packet.size, nullptr);
      else
        free(packet.data);
    }
    if (context.overflow.exchange(false))
      stop("Bluetooth event queue full; enable to retry");  // Cannot safely
                                                            // continue after
                                                            // losing control
                                                            // events.
    const auto s = snapshot();
    const auto now = millis();
    if (s.pairing && expired(now, context.since, 30000)) {
      Event reject;
      reject.kind = Confirm;
      handle(reject);
    } else if (s.state == Pan::State::RetryWait && context.retry.due(now))
      connectNow();
    else if (s.state == Pan::State::ObtainingIp &&
             expired(now, context.since, 30000)) {
      esp_pan_disconnect(context.handle);
      context.state(Pan::State::ObtainingIp, "DHCP timed out; reconnecting");
    } else if ((s.state == Pan::State::Connecting ||
                s.state == Pan::State::Starting) &&
               expired(now, context.since, 45000)) {
      // PAN connect has no cancellation API. Deinitialize before allowing a new
      // attempt.
      stop("Bluetooth connection timed out; enable to retry");
    }
    if (context.stopping && !context.deinitRequested) stop();
    if (context.stopping && context.deinitRequested &&
        expired(now, context.since, 1000)) {
      // The deinit completion can itself be the control event dropped on
      // overload.
      esp_pan_profile_status_t profile{};
      if (esp_pan_get_profile_status(&profile) == ESP_OK &&
          !profile.pan_inited && !profile.conn_num)
        context.finished = true;
    }
  }
  while (!cleanup()) vTaskDelay(pdMS_TO_TICKS(100));
  context.running = false;
  vTaskDelete(nullptr);
}
Pan::Result command(Event e) {
  if (!context.running.load() || !context.accepting.load() ||
      snapshot().state == Pan::State::Stopping)
    return Pan::Result::Busy;
  return context.post(e) ? Pan::Result::Ok : Pan::Result::Busy;
}
}  // namespace
Pan::Result Pan::begin() {
  bool expected = false;
  if (!context.running.compare_exchange_strong(expected, true)) {
    const auto current = status();
    return !current.enabled || current.state == State::Stopping ? Result::Busy
                                                                : Result::Ok;
  }
  xQueueReset(context.events);
  context.ready = context.stopping = context.deinitRequested =
      context.initRequested = context.finished = false;
  context.stopError = nullptr;
  context.stopRequested = false;
  context.scanning = context.connectAfterScan = false;
  context.retry = {};
  context.overflow = false;
  context.accepting = true;
  context.state(State::Starting, "Starting Bluetooth network");
  if (xTaskCreate(worker, "freeink-pan", 6144, nullptr, 2, nullptr) != pdPASS) {
    context.accepting = false;
    context.state(State::Error, "Not enough memory to start Bluetooth");
    portENTER_CRITICAL(&context.lock);
    context.status.enabled = false;
    portEXIT_CRITICAL(&context.lock);
    context.running = false;
    return Result::Failed;
  }
  return Result::Ok;
}
Pan::Result Pan::end() {
  if (context.running) context.stopRequested = true;
  return Result::Ok;
}
Pan::Result Pan::scan() {
  const auto state = status().state;
  if (state != State::Idle && state != State::RetryWait &&
      state != State::Error)
    return Result::Busy;
  return command(Event{Scan});
}
Pan::Result Pan::connect(const uint8_t address[6]) {
  if (!addressValid(address)) return Result::InvalidArgument;
  auto s = status();
  if (s.state != State::Idle && s.state != State::Scanning &&
      s.state != State::RetryWait && s.state != State::Error)
    return Result::Busy;
  Event e;
  e.kind = Connect;
  memcpy(e.device.address, address, 6);
  for (size_t i = 0; i < s.count; ++i)
    if (!memcmp(s.devices[i].address, address, 6)) e.device = s.devices[i];
  if (!e.device.name[0] && !memcmp(s.peer.address, address, 6))
    e.device = s.peer;
  return command(e);
}
Pan::Result Pan::disconnect() { return command(Event{Disconnect}); }
Pan::Result Pan::confirmPairing(bool accept) {
  if (!status().pairing) return Result::InvalidArgument;
  Event e;
  e.kind = Confirm;
  e.ok = accept;
  return command(e);
}
Pan::Result Pan::forget() { return command(Event{Forget}); }
Pan::Status Pan::status() const { return snapshot(); }
#else
Pan::Result Pan::begin() { return Result::Unsupported; }
Pan::Result Pan::end() { return Result::Unsupported; }
Pan::Result Pan::scan() { return Result::Unsupported; }
Pan::Result Pan::connect(const uint8_t[6]) { return Result::Unsupported; }
Pan::Result Pan::disconnect() { return Result::Unsupported; }
Pan::Result Pan::confirmPairing(bool) { return Result::Unsupported; }
Pan::Result Pan::forget() { return Result::Unsupported; }
Pan::Status Pan::status() const {
  Status s;
  return s;
}
#endif
