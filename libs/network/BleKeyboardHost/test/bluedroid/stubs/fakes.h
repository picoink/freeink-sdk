#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <deque>
#include <vector>
#include <map>
#include <string>
#define FREEINK_CAP_BLE_HID_HOST 1
#define FREEINK_BLE_BLUEDROID 1
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
inline uint32_t nowMs=100;
inline unsigned long millis(){return nowMs;}
inline void vTaskDelay(unsigned n){nowMs+=n;}
struct StaticQueue_t{size_t limit,size;std::deque<std::vector<uint8_t>> values;};
using QueueHandle_t=StaticQueue_t*;
inline QueueHandle_t xQueueCreateStatic(size_t n,size_t size,uint8_t*,StaticQueue_t* q){q->limit=n;q->size=size;return q;}
inline void xQueueReset(QueueHandle_t q){q->values.clear();}
inline int xQueueSend(QueueHandle_t q,const void* value,int){if(q->values.size()==q->limit)return 0;auto p=static_cast<const uint8_t*>(value);q->values.emplace_back(p,p+q->size);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* out,int){if(q->values.empty())return 0;memcpy(out,q->values.front().data(),q->size);q->values.pop_front();return 1;}
struct Preferences {
 static inline std::map<std::string,std::vector<uint8_t>> data;
 bool begin(const char*,bool=false){return true;} void end(){}
 uint8_t getUChar(const char* k,uint8_t v){return data[k].empty()?v:data[k][0];}
 size_t getBytes(const char* k,void* out,size_t n){n=std::min(n,data[k].size());memcpy(out,data[k].data(),n);return n;}
 size_t putUChar(const char* k,uint8_t v){data[k]={v};return 1;}
 size_t putBytes(const char* k,const void* value,size_t n){auto p=static_cast<const uint8_t*>(value);data[k]=std::vector<uint8_t>(p,p+n);return n;}
};
using esp_gatt_if_t=uint8_t; using esp_bd_addr_t=uint8_t[6];using esp_ble_addr_type_t=uint8_t;
using esp_gap_ble_cb_event_t=int;using esp_gattc_cb_event_t=int;
struct esp_bt_uuid_t{int len;union {uint16_t uuid16;}uuid;};
struct esp_gattc_char_elem_t{esp_bt_uuid_t uuid;uint16_t char_handle;uint8_t properties;};
struct esp_gattc_descr_elem_t{uint16_t handle;};
struct esp_ble_gattc_cancel_open_params_t{esp_gatt_if_t gattc_if;esp_bd_addr_t remote_bda;};
struct esp_ble_gatt_creat_conn_params_t{esp_ble_addr_type_t remote_addr_type;esp_bd_addr_t remote_bda;bool is_direct,is_aux;};
struct ScanConfig{int scan_type,scan_interval,scan_window;};
struct esp_ble_ext_scan_params_t{int own_addr_type,filter_policy,scan_duplicate,cfg_mask;ScanConfig uncoded_cfg,coded_cfg;};
constexpr int ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT=401;
struct esp_ble_gap_cb_param_t {
 struct{uint16_t status=0;uint8_t bd_addr[6]{};}remove_bond_dev_cmpl;
 struct Status{uint16_t status=0;};Status set_ext_scan_params,ext_scan_start,ext_scan_stop;
 struct{struct{uint8_t addr[6]{};uint8_t addr_type=0;int8_t rssi=0;int event_type=0;uint8_t adv_data_len=0;uint8_t adv_data[251]{};}params;}ext_adv_report;
 struct{struct{uint8_t bd_addr[6]{};}ble_req;struct{uint8_t bd_addr[6]{};bool success=false;}auth_cmpl;struct{uint8_t bd_addr[6]{};uint32_t passkey=0;}key_notif;}ble_security;
};
struct esp_ble_gattc_cb_param_t {
 struct{uint16_t app_id=0,status=0;}reg;
 struct{uint16_t status=0,conn_id=0;uint8_t remote_bda[6]{};}open,close;
 struct{uint16_t status=0;}cancel_open;
 struct{struct{esp_bt_uuid_t uuid;}srvc_id;uint16_t conn_id=0,start_handle=0,end_handle=0;}search_res;
 struct{uint16_t status=0,conn_id=0;}search_cmpl;
 struct{uint16_t status=0,conn_id=0,handle=0,value_len=0;uint8_t* value=nullptr;}read,write,notify;
 struct{uint16_t status=0,handle=0;}reg_for_notify;
 struct{uint8_t remote_bda[6]{};}srvc_chg;
};
constexpr int BLE_ADDR_TYPE_PUBLIC=0;
constexpr int BLE_SCAN_DUPLICATE_DISABLE=257;
constexpr int BLE_SCAN_FILTER_ALLOW_ALL=258;
constexpr int BLE_SCAN_TYPE_ACTIVE=259;
constexpr int ESP_BLE_ENC_KEY_MASK=1;
constexpr int ESP_BLE_GAP_EXT_SCAN_CFG_CODE_MASK=261;
constexpr int ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK=262;
constexpr int ESP_BLE_ID_KEY_MASK=2;
constexpr int ESP_BLE_SEC_ENCRYPT_NO_MITM=264;
constexpr int ESP_BLE_SM_AUTHEN_REQ_MODE=265;
constexpr int ESP_BLE_SM_IOCAP_MODE=266;
constexpr int ESP_BLE_SM_SET_INIT_KEY=267;
constexpr int ESP_BLE_SM_SET_RSP_KEY=268;
constexpr int ESP_GAP_BLE_AUTH_CMPL_EVT=269;
constexpr int ESP_GAP_BLE_EXT_ADV_REPORT_EVT=270;
constexpr int ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT=271;
constexpr int ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT=272;
constexpr int ESP_GAP_BLE_NC_REQ_EVT=273;
constexpr int ESP_GAP_BLE_PASSKEY_NOTIF_EVT=274;
constexpr int ESP_GAP_BLE_PASSKEY_REQ_EVT=275;
constexpr int ESP_GAP_BLE_SCAN_TIMEOUT_EVT=276;
constexpr int ESP_GAP_BLE_SEC_REQ_EVT=277;
constexpr int ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT=278;
constexpr int ESP_GATTC_CANCEL_OPEN_EVT=279;
constexpr int ESP_GATTC_CLOSE_EVT=280;
constexpr int ESP_GATTC_NOTIFY_EVT=281;
constexpr int ESP_GATTC_OPEN_EVT=282;
constexpr int ESP_GATTC_READ_CHAR_EVT=283;
constexpr int ESP_GATTC_READ_DESCR_EVT=284;
constexpr int ESP_GATTC_REG_EVT=285;
constexpr int ESP_GATTC_REG_FOR_NOTIFY_EVT=286;
constexpr int ESP_GATTC_SEARCH_CMPL_EVT=287;
constexpr int ESP_GATTC_SEARCH_RES_EVT=288;
constexpr int ESP_GATTC_SRVC_CHG_EVT=289;
constexpr int ESP_GATTC_UNREG_EVT=290;
constexpr int ESP_GATTC_WRITE_CHAR_EVT=291;
constexpr int ESP_GATTC_WRITE_DESCR_EVT=292;
constexpr int ESP_GATT_AUTH_REQ_NONE=293;
constexpr int ESP_GATT_CHAR_PROP_BIT_NOTIFY=16;
constexpr int ESP_GATT_IF_NONE=255;
constexpr int ESP_GATT_INVALID_ATTR_LEN=296;
constexpr int ESP_GATT_OK=0;
constexpr int ESP_GATT_WRITE_TYPE_NO_RSP=298;
constexpr int ESP_GATT_WRITE_TYPE_RSP=299;
constexpr int ESP_IO_CAP_OUT=0;
constexpr int ESP_LE_AUTH_BOND=1;
constexpr int ESP_OK=0;
constexpr int ESP_UUID_LEN_16=2;
inline std::map<std::string,int> calls, errors;
template<class... T> int esp_ble_confirm_reply(T...){++calls["esp_ble_confirm_reply"];return errors["esp_ble_confirm_reply"];}
template<class... T> int esp_ble_gap_security_rsp(T...){++calls["esp_ble_gap_security_rsp"];return errors["esp_ble_gap_security_rsp"];}
template<class... T> int esp_ble_gap_set_ext_scan_params(T...){++calls["esp_ble_gap_set_ext_scan_params"];return errors["esp_ble_gap_set_ext_scan_params"];}
template<class... T> int esp_ble_gap_set_security_param(T...){++calls["esp_ble_gap_set_security_param"];return errors["esp_ble_gap_set_security_param"];}
template<class... T> int esp_ble_gap_start_ext_scan(T...){++calls["esp_ble_gap_start_ext_scan"];return errors["esp_ble_gap_start_ext_scan"];}
template<class... T> int esp_ble_gap_stop_ext_scan(T...){++calls["esp_ble_gap_stop_ext_scan"];return errors["esp_ble_gap_stop_ext_scan"];}
template<class... T> int esp_ble_gattc_app_register(T...){++calls["esp_ble_gattc_app_register"];return errors["esp_ble_gattc_app_register"];}
template<class... T> int esp_ble_gattc_app_unregister(T...){++calls["esp_ble_gattc_app_unregister"];return errors["esp_ble_gattc_app_unregister"];}
template<class... T> int esp_ble_gattc_cache_refresh(T...){++calls["esp_ble_gattc_cache_refresh"];return errors["esp_ble_gattc_cache_refresh"];}
template<class... T> int esp_ble_gattc_cancel_open(T...){++calls["esp_ble_gattc_cancel_open"];return errors["esp_ble_gattc_cancel_open"];}
template<class... T> int esp_ble_gattc_close(T...){++calls["esp_ble_gattc_close"];return errors["esp_ble_gattc_close"];}
template<class... T> int esp_ble_gattc_enh_open(T...){++calls["esp_ble_gattc_enh_open"];return errors["esp_ble_gattc_enh_open"];}
template<class... T> int esp_ble_gattc_read_char(T...){++calls["esp_ble_gattc_read_char"];return errors["esp_ble_gattc_read_char"];}
template<class... T> int esp_ble_gattc_read_char_descr(T...){++calls["esp_ble_gattc_read_char_descr"];return errors["esp_ble_gattc_read_char_descr"];}
template<class... T> int esp_ble_gattc_register_for_notify(T...){++calls["esp_ble_gattc_register_for_notify"];return errors["esp_ble_gattc_register_for_notify"];}
template<class... T> int esp_ble_gattc_search_service(T...){++calls["esp_ble_gattc_search_service"];return errors["esp_ble_gattc_search_service"];}
template<class... T> int esp_ble_gattc_write_char(T...){++calls["esp_ble_gattc_write_char"];return errors["esp_ble_gattc_write_char"];}
template<class... T> int esp_ble_gattc_write_char_descr(T...){++calls["esp_ble_gattc_write_char_descr"];return errors["esp_ble_gattc_write_char_descr"];}
template<class... T> int esp_ble_passkey_reply(T...){++calls["esp_ble_passkey_reply"];return errors["esp_ble_passkey_reply"];}
template<class... T> int esp_ble_remove_bond_device(T...){++calls["esp_ble_remove_bond_device"];return errors["esp_ble_remove_bond_device"];}
template<class... T> int esp_ble_set_encryption(T...){++calls["esp_ble_set_encryption"];return errors["esp_ble_set_encryption"];}
inline void (*gapCb)(esp_gap_ble_cb_event_t,esp_ble_gap_cb_param_t*)=nullptr;
inline void (*gattCb)(esp_gattc_cb_event_t,esp_gatt_if_t,esp_ble_gattc_cb_param_t*)=nullptr;
inline int esp_ble_gap_register_callback(decltype(gapCb) cb){gapCb=cb;return 0;}
inline int esp_ble_gattc_register_callback(decltype(gattCb) cb){gattCb=cb;return 0;}
inline int esp_ble_gattc_get_all_char(esp_gatt_if_t,uint16_t,uint16_t,uint16_t,esp_gattc_char_elem_t* out,uint16_t* n,int){*n=3;out[0]={{ESP_UUID_LEN_16,{0x2A4B}},11,0};out[1]={{ESP_UUID_LEN_16,{0x2A4D}},12,16};out[2]={{ESP_UUID_LEN_16,{0x2A4E}},13,0};return 0;}
inline int esp_ble_gattc_get_descr_by_char_handle(esp_gatt_if_t,uint16_t,uint16_t,esp_bt_uuid_t id,esp_gattc_descr_elem_t* out,uint16_t* n){*n=1;out->handle=id.uuid.uuid16==0x2908?21:22;return 0;}
namespace freeink {
class BluetoothHost {public: enum class Profile {Pan=1,Hid=2};static inline unsigned leases=0;static inline bool releaseOk=true;
static bool acquire(Profile p){leases|=unsigned(p);return true;}
static bool held(Profile p){return leases&unsigned(p);}
static bool release(Profile p){if(!releaseOk)return false;leases&=~unsigned(p);return true;}
};}
