#pragma once
#include <atomic>
#include <array>
#include <cassert>
#include <condition_variable>
#include <mutex>
enum EpdDrawMode { MODE_GC16, MODE_GL16, MODE_DU };
enum EpdDrawError { EPD_DRAW_SUCCESS, EPD_DRAW_FAILED };
enum EpdInitOptions { EPD_LUT_1K };
enum { EPD_ROT_LANDSCAPE };
struct EpdBoardDefinition {};
struct EpdWaveform {};
struct EpdDisplay_t { const EpdWaveform* default_waveform = nullptr; };
struct EpdiyHighlevelState { uint8_t* front_fb = nullptr; const EpdWaveform* waveform = nullptr; };
namespace panel {
inline std::array<uint8_t, 64> framebuffer{};
inline std::mutex mutex;
inline std::condition_variable cv;
inline bool blocked = false, entered = false;
inline std::atomic<unsigned> calls{0}, clears{0}, inits{0}, deinits{0};
inline std::atomic<int> lastMode{0}, lastKind{0};
inline std::atomic<bool> fail{false};
inline const EpdWaveform* lastWaveform = nullptr;
inline void block() { std::lock_guard<std::mutex> lock(mutex); blocked = true; entered = false; }
inline void waitEntered() {
  std::unique_lock<std::mutex> lock(mutex);
  assert(cv.wait_for(lock, std::chrono::seconds(2), [] { return entered; }));
}
inline void release() { std::lock_guard<std::mutex> lock(mutex); blocked = false; cv.notify_all(); }
inline EpdDrawError refresh(int kind, EpdDrawMode mode) {
  std::unique_lock<std::mutex> lock(mutex);
  entered = true; cv.notify_all();
  cv.wait(lock, [] { return !blocked; });
  ++calls; lastKind = kind; lastMode = mode;
  return fail ? EPD_DRAW_FAILED : EPD_DRAW_SUCCESS;
}
}
inline float epd_ambient_temperature() { return 25; }
inline void epd_init(const EpdBoardDefinition*, const EpdDisplay_t*, EpdInitOptions) { ++panel::inits; }
inline void epd_deinit() { ++panel::deinits; }
inline void epd_set_vcom(uint16_t) {}
inline void epd_set_rotation(int) {}
inline EpdiyHighlevelState epd_hl_init(const EpdWaveform* wf) { return {panel::framebuffer.data(),wf}; }
inline uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState* state) { return state->front_fb; }
inline void epd_poweron() {}
inline void epd_poweroff() {}
inline EpdDrawError epd_clear_checked() { ++panel::clears; return EPD_DRAW_SUCCESS; }
inline EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState* s, EpdDrawMode mode, int) { panel::lastWaveform=s->waveform; return panel::refresh(0, mode); }
inline EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState* s, EpdDrawMode mode, int) { panel::lastWaveform=s->waveform; return panel::refresh(1, mode); }
inline EpdDrawError epd_hl_update_screen(EpdiyHighlevelState* s, EpdDrawMode mode, int) { panel::lastWaveform=s->waveform; return panel::refresh(2, mode); }
