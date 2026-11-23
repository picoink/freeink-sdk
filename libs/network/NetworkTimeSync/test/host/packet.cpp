#include <NetworkTimeSync.h>

#include <cassert>
#include <cstring>
#include <initializer_list>

#include "../../src/NtpPacket.h"
using namespace freeink::ntp_detail;
static void timestamp(uint8_t* packet, int64_t utc) {
  const auto seconds = uint32_t(utc + 2208988800LL);
  for (int i = 0; i < 4; ++i) packet[40 + i] = seconds >> (24 - 8 * i);
}
int main() {
  const uint8_t nonce[8]{1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t packet[48]{};
  packet[0] = 0x24;
  packet[1] = 2;
  memcpy(packet + 24, nonce, 8);
  int64_t utc = 123;
  timestamp(packet, 1795392000LL);
  assert(response(packet, 48, nonce, utc) && utc == 1795392000LL);
  assert(!response(nullptr, 48, nonce, utc) &&
         !response(packet, 47, nonce, utc));
  packet[24] ^= 1;
  assert(!response(packet, 48, nonce, utc));
  packet[24] ^= 1;
  for (auto first :
       {uint8_t(0xe4), uint8_t(0x23), uint8_t(0x14), uint8_t(0x2c)}) {
    packet[0] = first;
    assert(!response(packet, 48, nonce, utc));
  }
  packet[0] = 0x1c;
  assert(response(packet, 48, nonce, utc));  // NTPv3 server
  for (auto stratum : {0, 16, 255}) {
    packet[1] = stratum;
    assert(!response(packet, 48, nonce, utc));
  }
  packet[1] = 1;
  for (auto epoch : {1577836800LL, 2085978495LL, 2085978497LL, 4102444799LL}) {
    timestamp(packet, epoch);
    assert(response(packet, 48, nonce, utc) && utc == epoch);
  }
  for (auto epoch : {1577836799LL, 4102444800LL}) {
    timestamp(packet, epoch);
    assert(!response(packet, 48, nonce, utc));
  }
  memset(packet + 40, 0, 8);
  assert(!response(packet, 48, nonce, utc));
  auto& sync = freeink::NetworkTimeSync::instance();
  utc = 123;
  assert(!sync.start() && !sync.busy());
  sync.cancel();
  assert(!sync.takeResult(utc) && utc == 123);
}
