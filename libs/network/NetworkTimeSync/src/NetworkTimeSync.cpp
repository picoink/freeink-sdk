#include <NetworkTimeSync.h>

#include "NtpPacket.h"
using Sync = freeink::NetworkTimeSync;
Sync& Sync::instance() {
  static Sync value;
  return value;
}
#if defined(ARDUINO_ARCH_ESP32) && !defined(FREEINK_HOST)
#include <Arduino.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <atomic>
#include <cstdio>
#include <ctime>
namespace {
portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
Sync::Status result;
std::atomic<bool> running{false}, cancelled{false};
bool pending = false;
void finish(bool ok, int64_t utc, const char* message) {
  portENTER_CRITICAL(&guard);
  if (!cancelled.load()) {
    result.state = ok ? Sync::State::Succeeded : Sync::State::Failed;
    result.utc = utc;
    snprintf(result.message, sizeof(result.message), "%s", message);
    pending = ok;
    ++result.revision;
  }
  portEXIT_CRITICAL(&guard);
}
void query(void*) {
  const uint32_t started = millis();
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* resolved = nullptr;
  int fd = -1;
  int64_t utc = 0;
  bool ok = false;
  const char* message = "NTP DNS lookup failed";
  if (getaddrinfo(Sync::Server, "123", &hints, &resolved) == 0 && resolved) {
    if (!cancelled.load() && uint32_t(millis() - started) < 30000)
      fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd >= 0 && connect(fd, resolved->ai_addr, resolved->ai_addrlen) == 0) {
      timeval timeout{1, 0};
      if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) !=
              0 ||
          setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) !=
              0) {
        freeaddrinfo(resolved);
        close(fd);
        finish(false, 0, "NTP socket timeout could not be configured");
        running = false;
        vTaskDelete(nullptr);
        return;
      }
      message = "NTP timed out; Bluetooth IP may have no Internet access";
      uint8_t packet[48]{};
      packet[0] = 0x23;
      // A fresh nonce ties the reply to this query, even when the RTC is unset.
      esp_fill_random(packet + 40, 8);
      uint32_t sent = millis() - 5000;
      while (!cancelled.load() && uint32_t(millis() - started) < 30000) {
        if (uint32_t(millis() - sent) >= 5000) {
          if (send(fd, packet, sizeof(packet), 0) != sizeof(packet)) {
            message = "NTP send failed";
            break;
          }
          sent = millis();
        }
        uint8_t reply[512];
        const int count = recv(fd, reply, sizeof(reply), 0);
        if (count >= 48 &&
            freeink::ntp_detail::response(reply, count, packet + 40, utc)) {
          ok = true;
          break;
        }
      }
    } else if (!cancelled.load())
      message = "NTP socket could not start";
  }
  if (resolved) freeaddrinfo(resolved);
  if (fd >= 0) close(fd);
  finish(ok, utc, ok ? "NTP reply received from ntp.aliyun.com" : message);
  running = false;
  vTaskDelete(nullptr);
}
}  // namespace
bool Sync::start() {
  bool expected = false;
  if (!running.compare_exchange_strong(expected, true)) return false;
  portENTER_CRITICAL(&guard);
  cancelled = false;
  pending = false;
  result.state = State::Syncing;
  result.utc = 0;
  snprintf(result.message, sizeof(result.message),
           "Synchronizing with ntp.aliyun.com");
  ++result.revision;
  portEXIT_CRITICAL(&guard);
  if (xTaskCreate(query, "freeink-ntp", 4096, nullptr, 1, nullptr) != pdPASS) {
    finish(false, 0, "Not enough memory for NTP");
    running = false;
    return false;
  }
  return true;
}
bool Sync::busy() const { return running.load(); }
void Sync::cancel() {
  portENTER_CRITICAL(&guard);
  cancelled = true;
  pending = false;
  if (result.state == State::Syncing) {
    result.state = State::Idle;
    ++result.revision;
    snprintf(result.message, sizeof(result.message),
             "Time synchronization cancelled");
  }
  portEXIT_CRITICAL(&guard);
}
bool Sync::takeResult(int64_t& utc) {
  portENTER_CRITICAL(&guard);
  const bool ready = pending && !cancelled.load();
  if (ready) {
    utc = result.utc;
    pending = false;
  }
  portEXIT_CRITICAL(&guard);
  return ready;
}
Sync::Status Sync::status() const {
  portENTER_CRITICAL(&guard);
  auto value = result;
  portEXIT_CRITICAL(&guard);
  return value;
}
#else
bool Sync::start() { return false; }
bool Sync::busy() const { return false; }
void Sync::cancel() {}
bool Sync::takeResult(int64_t&) { return false; }
Sync::Status Sync::status() const { return {}; }
#endif
