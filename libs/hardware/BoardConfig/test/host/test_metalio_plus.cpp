#include <BoardConfig.h>
#include <cassert>
#include <cstdio>
using namespace freeink::metalio_plus;
int main() {
  static_assert(FREEINK_CAP_AUDIO && FREEINK_CAP_MIC && FREEINK_CAP_RTC && FREEINK_CAP_IMU);
  static_assert(BoardConfig::METALIO_EINK4_PLUS.audio.bclk == 42);
  static_assert(BoardConfig::METALIO_EINK4_PLUS.mic.data == 45);
  gpioLevels[EXPANDER_INT_GPIO] = HIGH;
  // A warm reset can leave the expander briefly unavailable. The boot entry
  // must complete its bounded retry before a caller can mount TF.
  const auto started = millis();
  Wire.expanderReadyAt = started + 100;
  BoardConfig::holdPowerRails();
  assert(ready() && Wire.ends == 2 && millis() - started == 180);
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
  readyFlag() = false;
  Wire.failExpander = true;
  const auto failedAt = millis();
  BoardConfig::holdPowerRails();
  assert(!ready() && millis() - failedAt == 3000);
  puts("Metalio Plus bounded boot retry, pins, latch write, RTC wake and sleep rollback passed");
}
