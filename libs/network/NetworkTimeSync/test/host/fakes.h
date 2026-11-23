#pragma once
#include <NetworkTimeSync.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <functional>
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
namespace fake {
inline uint32_t now = 0;
inline bool taskOk = true, dnsOk = true, reply = true, cancelReply = false;
inline int sockets = 0;
inline uint8_t sent[48]{};
inline void (*task)(void*) = nullptr;
}  // namespace fake
constexpr int pdPASS = 1;
inline uint32_t millis() { return fake::now; }
inline int xTaskCreate(void (*fn)(void*), const char*, unsigned, void*,
                       unsigned, void*) {
  if (!fake::taskOk) return 0;
  fake::task = fn;
  return pdPASS;
}
inline void vTaskDelete(void*) {}
inline void esp_fill_random(void* p, size_t n) { memset(p, 0x57, n); }
inline int fake_getaddrinfo(const char* name, const char* service,
                            const addrinfo*, addrinfo** out) {
  if (strcmp(name, "ntp.aliyun.com") || strcmp(service, "123") || !fake::dnsOk)
    return -1;
  static sockaddr address{};
  static addrinfo resolved{};
  resolved.ai_addr = &address;
  resolved.ai_addrlen = sizeof(address);
  *out = &resolved;
  return 0;
}
inline void fake_freeaddrinfo(addrinfo*) {}
inline int fake_socket(int, int, int) {
  ++fake::sockets;
  return 9;
}
inline int fake_connect(int, const sockaddr*, socklen_t) { return 0; }
inline int fake_setsockopt(int, int, int, const void*, socklen_t) { return 0; }
inline ssize_t fake_send(int, const void* p, size_t size, int) {
  memcpy(fake::sent, p, size);
  return size;
}
inline ssize_t fake_recv(int, void* p, size_t, int) {
  fake::now += 1000;
  if (!fake::reply) return -1;
  auto* reply = static_cast<uint8_t*>(p);
  memset(reply, 0, 48);
  reply[0] = 0x24;
  reply[1] = 2;
  memcpy(reply + 24, fake::sent + 40, 8);
  const uint32_t seconds = uint32_t(1795392000LL + 2208988800LL);
  for (int i = 0; i < 4; ++i) reply[40 + i] = seconds >> (24 - i * 8);
  if (fake::cancelReply) freeink::NetworkTimeSync::instance().cancel();
  return 48;
}
inline int fake_close(int) {
  --fake::sockets;
  return 0;
}
#define getaddrinfo fake_getaddrinfo
#define freeaddrinfo fake_freeaddrinfo
#define socket fake_socket
#define connect fake_connect
#define setsockopt fake_setsockopt
#define send fake_send
#define recv fake_recv
#define close fake_close
