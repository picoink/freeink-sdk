#include "../../src/Tps65185Startup.h"
#include <cassert>
#include <cstdio>
using freeink::metalio::waitTpsReady;
int main() {
  uint32_t now=0,calls=0;
  auto clock=[&]{return now;};auto delay=[&](uint32_t ms){now+=ms;};
  assert(waitTpsReady(clock,delay,[&]{++calls;assert(now>=3);return true;},50,3));
  assert(now==3 && calls==1);
  now=calls=0;
  assert(waitTpsReady(clock,delay,[&]{++calls;return now>=17;},50,3));
  assert(now==17 && calls==15);
  now=calls=0;
  assert(!waitTpsReady(clock,delay,[&]{++calls;now+=2;return false;},200));
  assert(now>=200 && now<203 && calls<=67);
  now=UINT32_MAX-10;const auto start=now;
  assert(waitTpsReady(clock,delay,[&]{return uint32_t(now-start)>=24;},200));
  assert(uint32_t(now-start)==24);
  puts("TPS65185 startup: minimum I2C delay, NACK retry, power-good deadline and timer wrap passed");
}
