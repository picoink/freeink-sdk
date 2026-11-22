#include <BoardConfig.h>
#include <Cx25601n.h>
#include <array>
#include <cassert>
#include <cstdio>

static std::array<std::array<uint8_t, 256>, 128> regs;
static int operations = 0, failAt = 0;
static unsigned unlockStep = 0;
static bool fail() { return ++operations == failAt; }
bool hostI2cWrite(uint8_t addr, const std::vector<uint8_t>& bytes) {
  if (fail()) return false;
  for (size_t i = 1; i < bytes.size(); ++i) {
    const uint8_t reg = bytes[0] + i - 1, value = bytes[i];
    if (addr == 0x6B && reg == 0x70) {
      const uint8_t sequence[] = {0, 0x50, 0x57, 0x44};
      if (value == 0) unlockStep = 0;
      if (value == sequence[unlockStep]) ++unlockStep;
      regs[addr][reg] = unlockStep == 4 ? 3 : 0;
      if (unlockStep == 4) unlockStep = 0;
    } else regs[addr][reg] = value;
  }
  return true;
}
bool hostI2cRead(uint8_t addr, uint8_t reg, uint8_t* out, size_t size) {
  if (fail()) return false;
  for (size_t i = 0; i < size; ++i) out[i] = regs[addr][reg + i];
  return true;
}
static void reset() {
  operations = failAt = unlockStep = 0;
  regs = {};
  regs[0x20][0] = regs[0x20][1] = 0xFF;
  freeink::metalio_plus::readyFlag() = true;
}
int main() {
  using Charger = freeink::Cx25601n;
  reset();
  Charger charger;
  assert(charger.begin());
  const int count = operations;
  assert(regs[0x6B][0x70] == 0 && (regs[0x6B][0x16] & 0x20));
  assert(((regs[0x6B][4] >> 3) | ((regs[0x6B][5] & 15) << 5)) * 10 == 4400);
  for (uint16_t nominal : {500, 1000, 1500}) {
    uint16_t actual = 0;
    assert(charger.setCurrent(nominal) && charger.getCurrent(actual));
    assert(actual == nominal / 80 * 80 && (regs[0x6B][0x16] & 0x20));
  }
  assert(!charger.setCurrent(0) && !charger.setCurrent(4000));
  Charger::Status status;
  regs[0x6B][0x1E] = 0x13;
  assert(Charger::readStatus(status) && status.charging() && status.externalPower());
  regs[0x6B][0x1E] = 0x10; // Real board: charging, but DP/DM classification is zero.
  assert(Charger::readStatus(status) && status.charging() && status.externalPower() && status.externalPowerKnown());
  regs[0x6B][0x1E] = 0;
  assert(Charger::readStatus(status) && !status.externalPowerKnown());
  regs[0x6B][0x1E] = 7;
  assert(Charger::readStatus(status) && !status.externalPower());
  assert(charger.service(3000, true, 0, true) == Charger::ServiceResult::Ok); // OTG is not an unplugged cell.
  regs[0x6B][0x1E] = 0;
  assert(charger.service(3000, true, 5, true) == Charger::ServiceResult::ShutdownRequested);
  assert(charger.service(0, false, 0, false) == Charger::ServiceResult::Ok);
  assert(regs[0x6B][0x14] & 4); // Unknown voltage retains termination.
  regs[0x6B][0x1E] = 3;
  assert(charger.service(4200, true, 80, true) == Charger::ServiceResult::Ok);
  assert(!(regs[0x6B][0x14] & 4) && (regs[0x6B][0x16] & 0x20));
  for (int i = 1; i <= count; ++i) {
    reset(); failAt = i;
    assert(!charger.begin() && !charger.present());
    assert(regs[0x6B][0x70] == 0);
    failAt = 0;
    assert(charger.begin());
  }
  assert(charger.enterShipMode() && regs[0x6B][0x18] == 1);
  printf("CX25601N current, status, termination, recharge, cutoff and %d init fault points passed\n", count);
}
