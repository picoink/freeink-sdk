#include <FreeInkUICore.h>

#include <chrono>
#include <thread>

#include "Arduino.h"
#include "HostMemory.h"
#include "HostRuntime.h"

namespace freeink::host {
Runtime& runtime() {
  // Firmware globals (including open font files) can use the backend during
  // process teardown. Keep it alive beyond those cross-translation-unit globals.
  static Runtime* r = [] {
    HostScope scope;
    return new Runtime;
  }();
  return *r;
}
std::string parseJson(const std::string& text, Json& out) { return picojson::parse(out, text); }
uint64_t wallMs() {
  static const auto epoch = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - epoch)
      .count();
}
uint64_t nowMs() { return wallMs() + runtime().timeOffset.load(); }
void pauseMs(uint64_t ms) {
  if (runtime().stop) throw Exit{0};
  if (ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  else
    std::this_thread::yield();
}
void beginFrame() {
  HostScope scope;
  runtime().drawingTexts.clear();
  runtime().drawingHits.clear();
}
void textDrawn(int x, int y, int w, int h, const char* text) {
  if (!text || !*text) return;
  HostScope scope;
  runtime().drawingTexts.push_back({x, y, w, h, text});
}
void hitPublished(int x, int y, int w, int h, int action, int value, int state) {
  HostScope scope;
  runtime().drawingHits.push_back({x, y, w, h, action, value, state});
}
void commitHitRegions() {
  HostScope scope;
  auto& r = runtime();
  std::lock_guard<std::mutex> lock(r.frameMutex);
  r.frame.hits = r.drawingHits;
}
void publishFrame(const uint8_t* bits, int width, int height) {
  HostScope scope;
  auto& r = runtime();
  Frame f;
  f.orientation = r.orientation;
  using O = ui::Orientation;
  const auto o = static_cast<O>(r.orientation);
  const bool portrait = o == O::Portrait || o == O::PortraitInverted;
  f.width = portrait ? height : width;
  f.height = portrait ? width : height;
  f.pixels.resize(size_t(f.width) * f.height);
  for (int y = 0; y < f.height; ++y)
    for (int x = 0; x < f.width; ++x) {
      int px = 0, py = 0;
      // Inverse of FreeInkUI's logicalToNative transform.
      switch (o) {
        case O::Portrait:
          px = y;
          py = height - 1 - x;
          break;
        case O::PortraitInverted:
          px = width - 1 - y;
          py = x;
          break;
        case O::LandscapeCounterClockwise:
          px = x;
          py = y;
          break;
        case O::LandscapeClockwise:
          px = width - 1 - x;
          py = height - 1 - y;
          break;
      }
      f.pixels[size_t(y) * f.width + x] =
          (bits[size_t(py) * (width / 8) + px / 8] & (0x80 >> (px % 8))) ? 255 : 0;
    }
  f.texts = r.drawingTexts;
  f.hits = r.drawingHits;
  if (r.inspect) f.state = r.inspect();
  std::lock_guard<std::mutex> lock(r.frameMutex);
  f.sequence = r.frame.sequence + 1;
  r.frame = std::move(f);
}
Frame snapshot() {
  HostScope scope;
  std::lock_guard<std::mutex> lock(runtime().frameMutex);
  return runtime().frame;
}
Json frameInfo(const Frame& f) {
  HostScope scope;
  Object result{{"frame", number(f.sequence)},
                {"width", number(f.width)},
                {"height", number(f.height)},
                {"orientation", number(f.orientation)},
                {"state", f.state}};
  Array texts, hits;
  for (const auto& t : f.texts)
    texts.emplace_back(Object{{"text", Json(t.text)},
                              {"x", number(t.x)},
                              {"y", number(t.y)},
                              {"width", number(t.w)},
                              {"height", number(t.h)}});
  for (const auto& h : f.hits)
    hits.emplace_back(Object{{"action", number(h.action)},
                             {"value", number(h.value)},
                             {"x", number(h.x)},
                             {"y", number(h.y)},
                             {"width", number(h.w)},
                             {"height", number(h.h)},
                             {"state", number(h.state)}});
  result["texts"] = Json(texts);
  result["controls"] = Json(hits);
  return Json(result);
}
bool fault(const char* name) { return boolean(field(field(runtime().devices, "faults"), name)); }
double deviceNumber(const char* name, double fallback) {
  return num(field(runtime().devices, name), fallback);
}
bool deviceBool(const char* name, bool fallback) { return boolean(field(runtime().devices, name), fallback); }
void setDevice(const char* name, Json value) {
  HostScope scope;
  runtime().devices.get<Object>()[name] = std::move(value);
}
}  // namespace freeink::host
unsigned long millis() { return freeink::host::nowMs(); }
unsigned long micros() { return millis() * 1000; }
void delay(unsigned long ms) { freeink::host::pauseMs(ms); }
void HostEsp::restart() { throw freeink::host::Exit{75}; }
