#pragma once
constexpr int ESP_SLEEP_WAKEUP_ALL=0, ESP_SLEEP_WAKEUP_UNDEFINED=0, ESP_SLEEP_WAKEUP_GPIO=7;
inline void esp_sleep_disable_wakeup_source(int) {}
inline int esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_UNDEFINED; }
