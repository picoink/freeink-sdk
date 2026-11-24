#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#define SOC_BT_CLASSIC_SUPPORTED 1
#define CONFIG_BT_BLUEDROID_ENABLED 1
#define CONFIG_BT_CLASSIC_ENABLED 1
#define CONFIG_BT_PAN_ENABLED 1
#define CONFIG_BT_BLE_ENABLED 1
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
using portMUX_TYPE = int;
namespace fake {
inline unsigned now = 0, ticks = 0, starts = 0, stops = 0, nets = 0,
                packets = 0, writes = 0, connects = 0;
inline unsigned confirms = 0, rejects = 0, bonds = 0, opens = 0,
                savedWrites = 0;
inline bool taskOk = true, initOk = true, nvsOk = true, profile = false;
inline int bluedroid = 0, controller = 0, lastMode = 0;
inline bool stopOk=true,hostEnableOk=true;
inline std::vector<uint8_t> saved, transmitted, received;
inline std::string name;
inline std::function<void()> onWait;
}  // namespace fake
inline uint32_t millis() { return fake::now; }
constexpr int pdPASS = 1;
inline unsigned pdMS_TO_TICKS(unsigned n) { return n; }
struct StaticQueue_t {
  size_t capacity = 0, item = 0;
  std::deque<std::vector<uint8_t>> data;
};
using QueueHandle_t = StaticQueue_t*;
inline QueueHandle_t xQueueCreateStatic(size_t count, size_t item, uint8_t*,
                                        StaticQueue_t* q) {
  q->capacity = count;
  q->item = item;
  return q;
}
inline int xQueueSend(QueueHandle_t q, const void* data, unsigned) {
  if (q->data.size() == q->capacity) return 0;
  auto* p = static_cast<const uint8_t*>(data);
  q->data.emplace_back(p, p + q->item);
  return pdPASS;
}
inline int xQueueReceive(QueueHandle_t q, void* data, unsigned wait) {
  if (q->data.empty() && wait) {
    fake::now += wait;
    assert(++fake::ticks < 10000);
    if (fake::onWait) fake::onWait();
  }
  if (q->data.empty()) return 0;
  memcpy(data, q->data.front().data(), q->item);
  q->data.pop_front();
  return pdPASS;
}
inline void xQueueReset(QueueHandle_t q) { q->data.clear(); }
inline int xTaskCreate(void (*)(void*), const char*, unsigned, void*, unsigned,
                       void*) {
  return fake::taskOk ? pdPASS : 0;
}
inline void vTaskDelete(void*) {}
inline void vTaskDelay(unsigned n) {
  fake::now += n;
  assert(++fake::ticks < 10000);
}
class Preferences {
 public:
  bool begin(const char*, bool) {
    ++fake::opens;
    return true;
  }
  void end() { --fake::opens; }
  size_t getBytesLength(const char*) { return fake::saved.size(); }
  size_t getBytes(const char*, void* p, size_t n) {
    memcpy(p, fake::saved.data(), n);
    return n;
  }
  void getString(const char*, char* p, size_t n) {
    snprintf(p, n, "%s", fake::name.c_str());
  }
  size_t putBytes(const char*, const void* p, size_t n) {
    ++fake::savedWrites;
    if (!fake::nvsOk) return 0;
    const auto* b = static_cast<const uint8_t*>(p);
    fake::saved.assign(b, b + n);
    return n;
  }
  void putString(const char*, const char* s) { fake::name = s; }
  void remove(const char* key) {
    if (!strcmp(key, "peer"))
      fake::saved.clear();
    else
      fake::name.clear();
  }
};
struct NetworkClass {
  bool begin() { return true; }
};
inline NetworkClass Network;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = -2,
              ESP_ERR_INVALID_STATE = -3;
constexpr int BT_MODE_CLASSIC_BT = 1, BT_MODE_BTDM = 2;
constexpr int ESP_BT_CONTROLLER_STATUS_IDLE=0, ESP_BLUEDROID_STATUS_UNINITIALIZED=0;
inline int esp_bt_controller_get_status(){return fake::controller;}
inline bool btStartMode(int mode) {
  ++fake::starts;fake::lastMode=mode;fake::controller=fake::initOk?2:0;
  return fake::initOk;
}
inline bool btStop() {
  ++fake::stops;
  if(!fake::stopOk)return false;fake::controller=0;return true;
}
constexpr int ESP_BLUEDROID_STATUS_ENABLED = 2,
              ESP_BLUEDROID_STATUS_INITIALIZED = 1;
inline int esp_bluedroid_init() {
  fake::bluedroid = 1;
  return 0;
}
inline int esp_bluedroid_enable() {
  if(!fake::hostEnableOk)return ESP_FAIL;
  fake::bluedroid = 2;
  return 0;
}
inline int esp_bluedroid_disable() {
  fake::bluedroid = 1;
  return 0;
}
inline int esp_bluedroid_deinit() {
  fake::bluedroid = 0;
  return 0;
}
inline int esp_bluedroid_get_status() { return fake::bluedroid; }
inline const uint8_t* esp_bt_dev_get_address() {
  static uint8_t address[6]{1, 2, 3, 4, 5, 6};
  return address;
}
using esp_bt_io_cap_t = int;
constexpr int ESP_BT_IO_CAP_IO = 1, ESP_BT_SP_IOCAP_MODE = 1;
constexpr int ESP_BT_NON_CONNECTABLE = 0, ESP_BT_NON_DISCOVERABLE = 0;
constexpr int ESP_BT_INQ_MODE_GENERAL_INQUIRY = 1, ESP_BT_STATUS_SUCCESS = 0;
enum esp_bt_gap_cb_event_t {
  ESP_BT_GAP_DISC_RES_EVT,
  ESP_BT_GAP_DISC_STATE_CHANGED_EVT,
  ESP_BT_GAP_CFM_REQ_EVT,
  ESP_BT_GAP_AUTH_CMPL_EVT,
  ESP_BT_GAP_PIN_REQ_EVT
};
enum {
  ESP_BT_GAP_DEV_PROP_RSSI,
  ESP_BT_GAP_DEV_PROP_BDNAME,
  ESP_BT_GAP_DEV_PROP_EIR,
  ESP_BT_GAP_DISCOVERY_STOPPED,
  ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,
  ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME
};
struct Property {
  int type, len;
  void* val;
};
struct esp_bt_gap_cb_param_t {
  struct {
    uint8_t bda[6]{};
    int num_prop = 0;
    Property* prop = nullptr;
  } disc_res;
  struct {
    int state;
  } disc_st_chg;
  struct {
    uint8_t bda[6]{};
    uint32_t num_val;
  } cfm_req;
  struct {
    uint8_t bda[6]{};
    int stat;
  } auth_cmpl;
  struct {
    uint8_t bda[6]{};
  } pin_req;
};
inline void (*gapCallback)(esp_bt_gap_cb_event_t,
                           esp_bt_gap_cb_param_t*) = nullptr;
inline int esp_bt_gap_register_callback(decltype(gapCallback) cb) {
  gapCallback = cb;
  return 0;
}
inline int esp_bt_gap_set_security_param(int, void*, size_t) { return 0; }
inline int esp_bt_gap_set_device_name(const char*) { return 0; }
inline int esp_bt_gap_set_scan_mode(int, int) { return 0; }
inline int esp_bt_gap_start_discovery(int, int, int) { return 0; }
inline int esp_bt_gap_cancel_discovery() {
  esp_bt_gap_cb_param_t p{};
  p.disc_st_chg.state = ESP_BT_GAP_DISCOVERY_STOPPED;
  gapCallback(ESP_BT_GAP_DISC_STATE_CHANGED_EVT, &p);
  return 0;
}
inline int esp_bt_gap_ssp_confirm_reply(uint8_t*, bool accept) {
  accept ? ++fake::confirms : ++fake::rejects;
  return 0;
}
inline int esp_bt_gap_pin_reply(uint8_t*, bool, unsigned, void*) {
  ++fake::rejects;
  return 0;
}
inline int esp_bt_gap_remove_bond_device(uint8_t*) {
  ++fake::bonds;
  return 0;
}
constexpr uint16_t ESP_PAN_INVALID_HANDLE = 0xffff;
constexpr int ESP_PAN_SUCCESS = 0, ESP_PAN_NEED_INIT = 1, ESP_PAN_ROLE_PANU = 1,
              ESP_PAN_ROLE_NAP = 2;
enum esp_pan_cb_event_t {
  ESP_PAN_INIT_EVT,
  ESP_PAN_DEINIT_EVT,
  ESP_PAN_OPEN_EVT,
  ESP_PAN_CLOSE_EVT,
  ESP_PAN_CONG_EVT,
  ESP_PAN_DATA_IND_EVT
};
struct esp_pan_cb_param_t {
  struct {
    int status;
  } init, deinit;
  struct {
    int status;
    uint16_t handle;
    uint8_t remote_bda[6]{};
  } open;
  struct {
    uint16_t handle;
  } close;
  struct {
    uint16_t handle;
    bool cong;
  } cong;
  struct {
    uint16_t handle, len;
    uint8_t *data, dst[6], src[6];
    uint16_t protocol;
    bool ext;
  } data_ind;
};
struct esp_pan_cfg_t {};
#define ESP_PAN_DEFAULT_CONFIG() \
  {                              \
  }
struct esp_pan_profile_status_t {
  bool pan_inited;
  uint8_t conn_num;
};
inline void (*panCallback)(esp_pan_cb_event_t, esp_pan_cb_param_t*) = nullptr;
inline int esp_pan_register_callback(decltype(panCallback) cb) {
  panCallback = cb;
  return 0;
}
inline int esp_pan_init(esp_pan_cfg_t*) {
  fake::profile = true;
  esp_pan_cb_param_t p{};
  panCallback(ESP_PAN_INIT_EVT, &p);
  return 0;
}
inline int esp_pan_deinit() {
  fake::profile = false;
  esp_pan_cb_param_t p{};
  panCallback(ESP_PAN_DEINIT_EVT, &p);
  return 0;
}
inline int esp_pan_get_profile_status(esp_pan_profile_status_t* p) {
  p->pan_inited = fake::profile;
  return 0;
}
inline int esp_pan_connect(uint8_t*, int, int) {
  ++fake::connects;
  return 0;
}
inline int esp_pan_disconnect(uint16_t handle) {
  esp_pan_cb_param_t p{};
  p.close.handle = handle;
  panCallback(ESP_PAN_CLOSE_EVT, &p);
  return 0;
}
inline int esp_pan_write(uint16_t, const uint8_t*, const uint8_t*, uint16_t,
                         size_t n, const uint8_t* p, bool) {
  ++fake::writes;
  fake::transmitted.assign(p, p + n);
  return 0;
}
struct esp_netif_t {};
struct esp_netif_driver_base_t {
  esp_netif_t* netif;
  esp_err_t (*post_attach)(esp_netif_t*, void*);
};
struct esp_netif_driver_ifconfig_t {
  void* handle;
  esp_err_t (*transmit)(void*, void*, size_t);
  void (*driver_free_rx_buffer)(void*, void*);
};
struct esp_netif_inherent_config_t {
  const char *if_key, *if_desc;
  int route_prio;
};
#define ESP_NETIF_INHERENT_DEFAULT_ETH() \
  {                                      \
  }
#define ESP_NETIF_NETSTACK_DEFAULT_ETH nullptr
struct esp_netif_config_t {
  esp_netif_inherent_config_t* base;
  void* stack;
};
inline esp_netif_t* esp_netif_new(esp_netif_config_t*) {
  ++fake::nets;
  return new esp_netif_t;
}
inline void esp_netif_destroy(esp_netif_t* p) {
  --fake::nets;
  delete p;
}
inline int esp_netif_attach(esp_netif_t* n, void* glue) {
  return static_cast<esp_netif_driver_base_t*>(glue)->post_attach(n, glue);
}
inline int esp_netif_set_mac(esp_netif_t*, uint8_t*) { return 0; }
inline int esp_netif_set_driver_config(esp_netif_t*,
                                       esp_netif_driver_ifconfig_t*) {
  return 0;
}
inline int esp_netif_set_default_netif(esp_netif_t*) { return 0; }
inline void esp_netif_action_start(esp_netif_t*, void*, int, void*) {}
inline void esp_netif_action_stop(esp_netif_t*, void*, int, void*) {}
inline void esp_netif_action_connected(esp_netif_t*, void*, int, void*) {}
inline void esp_netif_action_disconnected(esp_netif_t*, void*, int, void*) {}
inline int esp_netif_receive(esp_netif_t*, void* data, size_t n, void*) {
  ++fake::packets;
  auto* p = static_cast<uint8_t*>(data);
  fake::received.assign(p, p + n);
  free(data);
  return 0;
}
using esp_event_base_t = int;
using esp_event_handler_instance_t = void*;
constexpr int IP_EVENT = 1, IP_EVENT_ETH_GOT_IP = 2, IP_EVENT_ETH_LOST_IP = 3;
struct ip_event_got_ip_t {
  esp_netif_t* esp_netif;
  struct {
    uint8_t ip[4];
  } ip_info;
};
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) (*p)[0], (*p)[1], (*p)[2], (*p)[3]
inline int esp_event_handler_instance_register(int, int,
                                               void (*)(void*, int, int, void*),
                                               void*, void** h) {
  *h = reinterpret_cast<void*>(1);
  return 0;
}
inline int esp_event_handler_instance_unregister(int, int, void*) { return 0; }
