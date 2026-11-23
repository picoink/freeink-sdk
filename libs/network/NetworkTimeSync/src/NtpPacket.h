#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace freeink::ntp_detail {
inline uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | p[3];
}
inline bool response(const uint8_t* p, size_t size, const uint8_t nonce[8],
                     int64_t& utc) {
  if (!p || size < 48 || (p[0] >> 6) == 3 || (p[0] & 7) != 4 ||
      ((p[0] >> 3) & 7) < 3 || ((p[0] >> 3) & 7) > 4 || p[1] == 0 ||
      p[1] > 15 || memcmp(p + 24, nonce, 8))
    return false;
  const uint32_t seconds = be32(p + 40);
  if (!seconds && !be32(p + 44)) return false;
  // NTP era wraps in 2036. This firmware's RTC/application range is 2020..2099.
  int64_t value = int64_t(seconds) - 2208988800LL;
  if (value < 1577836800LL) value += 4294967296LL;
  if (value < 1577836800LL || value >= 4102444800LL) return false;
  utc = value;
  return true;
}
}  // namespace freeink::ntp_detail
