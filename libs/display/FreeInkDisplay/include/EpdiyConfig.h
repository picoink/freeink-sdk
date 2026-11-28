#pragma once

#include <BoardConfig.h>

#if FREEINK_DRIVER_EPDIY
#include <epdiy.h>

namespace freeink {

// Board wiring for EpdiyDriver. The board owns its EPDiy board definition
// (power sequencing, LCD bus pins and timing); the SDK owns the framebuffer
// conversion and refresh policy. Geometry comes from the ACTIVE BoardProfile and
// must match `display`.
struct EpdiyConfig {
  const EpdBoardDefinition* board;
  const EpdDisplay_t* display;
  const EpdWaveform* waveform;    // nullptr = display->default_waveform
  enum EpdInitOptions initOptions;
  int vcomMv;                     // VCOM magnitude in mV handed to the board; 0 = board default
  int temperatureC;               // waveform temperature when the board reports none
  bool deferredRefresh = false;   // opt-in SDK worker; other boards retain synchronous behavior
  // Optional complete board waveform for full-panel conditioning and redraw.
  // Must clean unchanged pixels too; a white-only erase is not a full refresh.
  // Unknown optical state still takes the checked clear/recovery path.
  const EpdWaveform* fullRefreshWaveform = nullptr;
  enum EpdDrawMode fullRefreshMode = MODE_GC16;
  const EpdWaveform* readingWaveform = nullptr; // Four-gray targets, all sixteen source levels.
};

}  // namespace freeink

#endif  // FREEINK_DRIVER_EPDIY
