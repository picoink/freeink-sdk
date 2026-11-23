#pragma once
#include <cstdint>
namespace freeink {
// One-shot NTP query. The caller applies the returned UTC time on its own loop.
// Persistent singleton; cancelling never leaves a callback pointing at a
// caller.
class NetworkTimeSync {
 public:
  enum class State : uint8_t { Idle, Syncing, Succeeded, Failed };
  struct Status {
    State state = State::Idle;
    int64_t utc = 0;
    uint32_t revision = 0;
    char message[96] = "Not synchronized";
  };
  static NetworkTimeSync& instance();
  bool start();
  bool busy() const;
  void cancel();
  bool takeResult(int64_t& utc);
  Status status() const;
  static constexpr const char* Server = "ntp.aliyun.com";

 private:
  NetworkTimeSync() = default;
};
}  // namespace freeink
