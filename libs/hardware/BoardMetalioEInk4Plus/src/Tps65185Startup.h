#pragma once
#include <stdint.h>

namespace freeink::metalio {
// TI TPS65185 SLVSAQ8G Figure 2 requires >=1.8 ms from WAKEUP to I2C.
// Keep 3 ms before probing, then wait for acknowledgement rather than assuming
// every board is ready at that minimum. All timeouts measure elapsed time.
template<class Clock, class Delay, class Ready>
bool waitTpsReady(Clock clock, Delay delay, Ready ready, uint32_t timeout,
                  uint32_t minimum = 0) {
  const uint32_t start=clock();
  if(minimum)delay(minimum);
  do {
    if(ready())return true;
    delay(1);
  } while(uint32_t(clock()-start)<timeout);
  return false;
}
}
