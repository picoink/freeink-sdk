#pragma once
#include <cstdint>
#include <cstddef>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, SDMMC_FREQ_HIGHSPEED = 40000, SDMMC_SLOT_FLAG_INTERNAL_PULLUP = 1;
inline const char* esp_err_to_name(int) { return "test"; }
struct sdmmc_command_t { uint32_t opcode; esp_err_t error = ESP_OK; };
struct sdmmc_host_t { int slot = 0, max_freq_khz = 0; esp_err_t (*do_transaction)(int, sdmmc_command_t*) = nullptr; };
struct sdmmc_slot_config_t { int width = 4, clk = 0, cmd = 0, d0 = 0, d1 = 0, d2 = 0, d3 = 0, flags = 0; };
#define SDMMC_HOST_DEFAULT() sdmmc_host_t{}
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
inline int hosts = 0, initCalls = 0, readCalls = 0, writeCalls = 0;
inline bool initFail = false, readFail = false, writeFail = false, commandFail = false;
inline int sdmmc_host_init() { ++hosts; return ESP_OK; }
inline int sdmmc_host_init_slot(int, const sdmmc_slot_config_t*) { return ESP_OK; }
inline int sdmmc_host_deinit() { --hosts; return ESP_OK; }
inline esp_err_t sdmmc_host_do_transaction(int, sdmmc_command_t* command) {
  if (commandFail) command->error = ESP_FAIL;
  return writeFail ? ESP_FAIL : ESP_OK;
}
