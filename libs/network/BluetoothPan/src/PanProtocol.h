#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace freeink::pan_detail {
constexpr size_t Header = 14, Mtu = 1500;
inline bool frame(const uint8_t* data, size_t size) {
  return data && size > Header && size <= Header + Mtu;
}
inline bool expired(uint32_t now, uint32_t since, uint32_t interval) {
  return uint32_t(now - since) >= interval;
}
struct Retry {
  bool enabled = false;
  uint8_t attempts = 0;
  uint32_t at = 0, delay = 2000;
  void schedule(uint32_t now) {
    static constexpr uint32_t delays[] = {2000, 5000, 10000, 30000};
    at = now;
    delay = delays[attempts < 3 ? attempts : 3];
    if (attempts < 3) ++attempts;
  }
  bool due(uint32_t now) const { return enabled && expired(now, at, delay); }
};
inline bool addressValid(const uint8_t* address) {
  if (!address) return false;
  uint8_t any = 0, all = 255;
  for (unsigned i = 0; i < 6; ++i) {
    any |= address[i];
    all &= address[i];
  }
  return any && all != 255;
}
}  // namespace freeink::pan_detail
