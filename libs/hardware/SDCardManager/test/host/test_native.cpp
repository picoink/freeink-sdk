#include <SdmmcBlockDevice.h>
#include <sdmmc_cmd.h>
#include <esp_heap_caps.h>
#include <array>
#include <cstdio>
int main() {
  freeink::SdmmcBlockDevice card;
  const BoardConfig::SdmmcPins pins{24,25,20,21,22,23,4};
  initFail = true;
  assert(!card.begin(pins) && initCalls == 4 && readCalls == 0 && hosts == 0 && dmaBuffers == 0);
  initFail = false; allocationFails = true;
  assert(!card.begin(pins) && hosts == 0);
  allocationFails = false; readFail = true;
  assert(!card.begin(pins) && hosts == 0 && dmaBuffers == 0);
  readFail = false;
  assert(card.begin(pins) && card.sectorCount() == 64 && hosts == 1);
  assert(card.begin(pins) && hosts == 1);
  std::array<uint8_t, 512 * 9 + 1> buffer{};
  assert(card.readSectors(1, buffer.data() + 1, 9)); // Unaligned caller uses DMA bounce buffer.
  assert(card.writeSectors(1, buffer.data() + 1, 9) && largestTransfer == 8);
  assert(!card.readSectors(63, buffer.data(), 2) && !card.writeSectors(UINT32_MAX, buffer.data(), 1));
  writeFail = true; assert(!card.writeSectors(0, buffer.data(), 2));
  writeFail = false; commandFail = true; assert(!card.writeSectors(0, buffer.data(), 2));
  commandFail = false; assert(card.writeSectors(0, buffer.data(), 2));
  card.end(); card.end(); assert(hosts == 0 && dmaBuffers == 0 && card.sectorCount() == 0);
  assert(!card.readSector(0, buffer.data()));
  puts("SDMMC init failures, DMA alignment, bounds, chunking, swallowed write errors and cleanup passed");
}
