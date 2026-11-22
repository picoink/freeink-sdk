#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "picojson.h"

namespace freeink::host {
using Json = picojson::value;
using Object = picojson::object;
using Array = picojson::array;
inline Json number(double n) { return Json(n); }
inline double num(const Json& v, double fallback = 0) { return v.is<double>() ? v.get<double>() : fallback; }
inline std::string str(const Json& v, const std::string& fallback = "") {
  return v.is<std::string>() ? v.get<std::string>() : fallback;
}
inline bool boolean(const Json& v, bool fallback = false) { return v.is<bool>() ? v.get<bool>() : fallback; }
inline const Json& field(const Json& v, const std::string& k) {
  static const Json nil;
  return v.is<Object>() ? v.get(k) : nil;
}
std::string parseJson(const std::string& text, Json& out);
struct Exit {
  int code;
};
struct Sleep {};
struct Text {
  int x, y, w, h;
  std::string text;
};
struct Hit {
  int x, y, w, h, action, value, state;
};
struct Frame {
  uint64_t sequence = 0;
  int width = 0, height = 0, orientation = 0;
  std::vector<uint8_t> pixels;  // logical, 8-bit luminance; owned by host
  std::vector<Text> texts;
  std::vector<Hit> hits;
  Json state;
};
struct Runtime {
  std::string sdRoot, stateRoot, outputRoot;
  std::atomic<bool> stop{false}, sleeping{false}, ready{false}, powerPressed{false};
  std::atomic<uint64_t> heartbeat{0}, timeOffset{0};
  std::mutex frameMutex;
  Frame frame;
  std::vector<Text> drawingTexts;
  std::vector<Hit> drawingHits;
  int orientation = 0;
  Json devices{Object{}};  // only accessed on the firmware thread
  std::function<Json()> inspect;
  uint64_t rtcEpoch = 1795392000, rtcSetAt = 0, wakeAt = 0;
  uint64_t displayFullMs = 0, displayFastMs = 0;
  uint64_t lastLoopMs = 0, maxLoopMs = 0, maxCommandWaitMs = 0;
  bool rtcFired = false;
};
Runtime& runtime();
uint64_t nowMs();
uint64_t wallMs();
void pauseMs(uint64_t ms);
void beginFrame();
void textDrawn(int x, int y, int w, int h, const char* text);
void hitPublished(int x, int y, int w, int h, int action, int value, int state);
void publishFrame(const uint8_t* bits, int width, int height);
Frame snapshot();
Json frameInfo(const Frame& f);
bool fault(const char* name);
double deviceNumber(const char* name, double fallback);
bool deviceBool(const char* name, bool fallback);
void setDevice(const char* name, Json value);
void ioCost(size_t bytes);
void serviceDevices();
}  // namespace freeink::host
