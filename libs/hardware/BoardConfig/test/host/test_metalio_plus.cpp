#include <BoardConfig.h>
#include <cassert>
#include <cstdio>
using namespace freeink::metalio_plus;
int main() {
  static_assert(FREEINK_CAP_AUDIO && FREEINK_CAP_MIC && FREEINK_CAP_RTC && FREEINK_CAP_IMU);
  static_assert(BoardConfig::METALIO_EINK4_PLUS.audio.bclk == 42);
  static_assert(BoardConfig::METALIO_EINK4_PLUS.mic.data == 45);
  gpioLevels[EXPANDER_INT_GPIO] = HIGH;
  assert(ensureBooted());
  const auto boot = outputLatch();
  Wire.failExpander = true;
  assert(!setOutput(PIN_MOTOR, true) && outputLatch() == boot);
  Wire.failExpander = false;
  assert(prepareWakeOnPowerKey(true));
  assert((Wire.expander[6] | (Wire.expander[7] << 8)) == 0xC000); // RTC and power stay inputs.
  assert(!(outputLatch() & ((1 << PIN_SCREEN_POWER) | (1 << PIN_TOUCH_RESET) | (1 << PIN_BT_AMP_POWER))));
  assert(restoreAfterSleepAbort() && outputLatch() == boot);
  assert((Wire.expander[6] | (Wire.expander[7] << 8)) == uint16_t(~OUTPUTS));
  assert(prepareWakeOnPowerKey(false));
  assert((Wire.expander[6] | (Wire.expander[7] << 8)) == 0x8000);
  assert(restoreAfterSleepAbort());
  gpioLevels[EXPANDER_INT_GPIO] = LOW;
  assert(!prepareWakeOnPowerKey(true));
  assert(restoreAfterSleepAbort() && outputLatch() == boot);
  puts("Metalio Plus pins, failed latch write, RTC wake routing and sleep rollback passed");
}
