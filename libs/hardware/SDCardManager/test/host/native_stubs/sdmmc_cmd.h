#pragma once
#include <driver/sdmmc_host.h>
#include <cassert>
#include <cstring>
struct sdmmc_card_t { struct { uint32_t capacity; } csd; sdmmc_host_t host; };
inline size_t largestTransfer = 0;
inline int sdmmc_card_init(const sdmmc_host_t* host, sdmmc_card_t* card) {
  ++initCalls; assert(card->csd.capacity == 0); card->csd.capacity = 64; card->host = *host;
  return initFail ? ESP_FAIL : ESP_OK;
}
inline int sdmmc_read_sectors(sdmmc_card_t*, void* data, size_t sector, size_t count) {
  ++readCalls; assert(reinterpret_cast<uintptr_t>(data) % 64 == 0 && count <= 8 && sector + count <= 64);
  if (readFail) return ESP_FAIL;
  memset(data, int(sector), count * 512); return ESP_OK;
}
inline int sdmmc_write_sectors(sdmmc_card_t* card, const void* data, size_t sector, size_t count) {
  ++writeCalls; if (count > largestTransfer) largestTransfer = count;
  assert(reinterpret_cast<uintptr_t>(data) % 64 == 0 && count <= 8 && sector + count <= 64);
  sdmmc_command_t command{count > 1 ? 25U : 24U};
  card->host.do_transaction(0, &command);
  // Model IDF's recovery overwriting the data transfer error with ACMD22 success.
  return ESP_OK;
}
