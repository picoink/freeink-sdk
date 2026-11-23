#include <Arduino.h>
#include <USBMSC.h>
#include <esp32-hal-bt.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp_bt_main.h>
#include <esp_pan_api.h>

USBMSC storage;
void setup() {
  btStartMode(BT_MODE_CLASSIC_BT);
  esp_bluedroid_init();
  esp_bluedroid_enable();
  esp_pan_cfg_t config = ESP_PAN_DEFAULT_CONFIG();
  esp_pan_init(&config);
  storage.begin(1, 512);
}
void loop() { delay(1000); }
