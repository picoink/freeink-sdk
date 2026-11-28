#include <Arduino.h>
#include <USBMSC.h>
#include <esp32-hal-bt.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp_bt_main.h>
#include <esp_pan_api.h>
#include <esp_gattc_api.h>
#include <esp_gap_ble_api.h>

USBMSC storage;
void gap(esp_gap_ble_cb_event_t,esp_ble_gap_cb_param_t*) {}
void gatt(esp_gattc_cb_event_t,esp_gatt_if_t,esp_ble_gattc_cb_param_t*) {}
void setup() {
  btStartMode(BT_MODE_BTDM);
  esp_bluedroid_init();
  esp_bluedroid_enable();
  esp_pan_cfg_t config = ESP_PAN_DEFAULT_CONFIG();
  esp_pan_init(&config);
  esp_ble_gap_register_callback(gap);
  esp_ble_gattc_register_callback(gatt);
  esp_ble_gattc_app_register(1);
  storage.begin(1, 512);
}
void loop() { delay(1000); }
