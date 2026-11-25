#include "EpdiyDriver.h"

#if FREEINK_DRIVER_EPDIY

#include <esp_heap_caps.h>
#if __has_include(<ResourceMemory.h>)
#include <ResourceMemory.h>
#include <ResourceCoordinator.h>
#endif
#include <freertos/idf_additions.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <cstring>
#include <array>
#include <epd_metrics.h>

#if EPD_LINE_QUEUE_PSRAM && __has_include(<ResourceMemory.h>)
extern "C" void* epd_allocate_line_queue(size_t bytes) {
  return freeink::resources::allocateAligned(bytes,16,freeink::resources::Category::System,0,freeink::resources::Lifetime::System);
}
extern "C" void epd_release_line_queue(void* pointer) {freeink::resources::releaseAligned(pointer);}
#endif

#ifndef FREEINK_EPDIY_CONFIG
#error \
    "FREEINK_DRIVER_EPDIY requires a board config: define `const EpdiyConfig& yourConfig();` in namespace freeink and build with -DFREEINK_EPDIY_CONFIG=yourConfig"
#endif

namespace freeink {

const EpdiyConfig& FREEINK_EPDIY_CONFIG();

namespace {

// EPDiy 4 bpp: two pixels per byte, even pixel in the low nibble, 0 = black,
// 15 = white.
constexpr uint8_t kBlack = 0x0, kDark = 0x5, kLight = 0xA, kWhite = 0xF;
constexpr std::array<uint8_t,256> grayNibbles() {
  std::array<uint8_t,256> values{};
  for (unsigned i=0;i<values.size();++i) values[i]=(i+8)/17;
  return values;
}
constexpr auto gGrayNibble=grayNibbles();

// One 1 bpp byte (MSB = leftmost pixel, set = white) -> four 4 bpp bytes.
constexpr std::array<uint32_t, 256> buildExpand() {
  std::array<uint32_t, 256> values{};
  for (int b = 0; b < 256; ++b) {
    uint32_t out = 0;
    for (int pair = 0; pair < 4; ++pair) {
      const uint8_t even = (b & (0x80 >> (pair * 2))) ? kWhite : kBlack;
      const uint8_t odd = (b & (0x40 >> (pair * 2))) ? kWhite : kBlack;
      out |= static_cast<uint32_t>(even | (odd << 4)) << (pair * 8);
    }
    values[b] = out;
  }
  return values;
}
constexpr auto gExpand = buildExpand();

enum EpdDrawMode modeFor(RefreshMode mode) {
  switch (mode) {
    case RefreshMode::Full:
    case RefreshMode::Half:
    case RefreshMode::TextClean:
      return MODE_GC16;
    case RefreshMode::TextStatus:
    case RefreshMode::Text:
    case RefreshMode::Text16:
    case RefreshMode::Fast:
      break;
  }
  return MODE_DU;
}

}  // namespace

EpdiyDriver::EpdiyDriver(const EpdiyConfig& cfg) : _cfg(cfg) {}
EpdiyDriver::~EpdiyDriver() { stopWorker(); }

void EpdiyDriver::runWorker(void* context) {
  auto& driver = *static_cast<EpdiyDriver*>(context);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (driver._stop.load()) break;
    driver.update(driver._lastBaseMode, driver._jobFull);
    driver._working.store(false);
  }
  driver._exited.store(true); // No further access to the driver after this point.
  vTaskDeleteWithCaps(nullptr);
}
void EpdiyDriver::stopWorker() {
  if (!_worker) return;
  while (_working.load()) delay(1);
  _stop.store(true); xTaskNotifyGive(_worker);
  while (!_exited.load()) delay(1);
  _worker = nullptr;
}


PanelGeometry EpdiyDriver::geometry() const {
  const uint16_t w = BoardConfig::ACTIVE.displayWidth;
  const uint16_t h = BoardConfig::ACTIVE.displayHeight;
  const uint16_t wb = w / 8;
  return {w, h, wb, static_cast<uint32_t>(wb) * h};
}

int EpdiyDriver::temperature() const {
  const float t = epd_ambient_temperature();
  return (t > 0.0f && t < 50.0f) ? static_cast<int>(t) : _cfg.temperatureC;
}

void EpdiyDriver::begin(EpdBus& bus) {
  (void)bus;
  if (_ready) return;
  epd_init(_cfg.board, _cfg.display, _cfg.initOptions);
  if (_cfg.vcomMv > 0) epd_set_vcom(static_cast<uint16_t>(_cfg.vcomMv));
  // Native scan order; the host renders in the panel's own 1216x684 frame.
  epd_set_rotation(EPD_ROT_LANDSCAPE);
  if (_hl.front_fb == nullptr) {
    _hl = epd_hl_init(_cfg.waveform ? _cfg.waveform : _cfg.display->default_waveform);
  }
  if(_cfg.readingWaveform && !_rawTarget) {
#if __has_include(<ResourceMemory.h>)
    _rawTarget=static_cast<uint8_t*>(resources::Memory::get().allocate(size_t(geometry().width)*geometry().height/2,resources::Category::System,0,resources::Lifetime::System));
#else
    _rawTarget=static_cast<uint8_t*>(heap_caps_malloc(size_t(geometry().width)*geometry().height/2,MALLOC_CAP_SPIRAM));
#endif
  }
  // Gray8 readers do not use legacy monochrome overlay planes. Allocate them
  // only when requested, leaving room for immutable phase preparation.
  if(!_cfg.readingWaveform)ensureOverlayPlanes();
#if __has_include(<ResourceMemory.h>)
  auto account=[](const void* pointer,size_t bytes){return !pointer || resources::Memory::get().accountPermanent(pointer,bytes+32);};
  const size_t frameBytes=size_t(geometry().width)*geometry().height/2;
  const bool accounted=account(_hl.front_fb,frameBytes) && account(_hl.back_fb,frameBytes) &&
    account(_hl.difference_fb,2*frameBytes);
#else
  const bool accounted=true;
#endif
  _ready = _hl.front_fb != nullptr && accounted;
  _faulted = false; _panelKnown = false;
  if (_ready && _cfg.deferredRefresh && !_worker) {
    _stop.store(false); _exited.store(false);
    if (xTaskCreatePinnedToCoreWithCaps(runWorker, "fi_present", 8192, this, 2,
        &_worker, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
      _worker = nullptr; _exited.store(true);
    }
  }
}

void EpdiyDriver::deepSleep(EpdBus& bus) {
  (void)bus;
  stopWorker();
  if (!_ready) return;
  epd_poweroff();
  epd_deinit();
  _ready = false;
}

// Rails up only for the waveform; the panel is powered off between refreshes.
// A differential update only drives pixels EPDiy's previous frame says changed,
// so the first draw clears the glass and draws from white. A board can supply
// its white-conditioning waveform for subsequent full refreshes.
bool EpdiyDriver::update(enum EpdDrawMode mode, bool full) {
#if __has_include(<ResourceCoordinator.h>)
  // A foreground flash caller may be waiting for this scan. Release the
  // barrier here, without depending on that caller returning to the UI loop.
  struct Complete {~Complete(){resources::Coordinator::get().request(resources::Coordinator::Barrier::Display,false);}} complete;
#endif
  // An underrun leaves unknown optical state. Stop at the hardware boundary:
  // automatic UI retries must not repeatedly clear/flash a faulty panel path.
  // Only an explicit resync (AI clean refresh) rearms this driver.
  if (_faulted) { _queuedAt=0; _success.store(false); return false; }
#if CONFIG_PM_ENABLE
  esp_pm_lock_handle_t awake = nullptr;
  if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "epdiy-present", &awake) == ESP_OK)
    esp_pm_lock_acquire(awake);
#endif
  const auto workerAt=micros();
  _metrics={}; epd_metrics_reset();
  _metrics.queue_us=_queuedAt ? uint32_t(workerAt-_queuedAt) : 0;
  _queuedAt=0;
  epd_poweron();
  const auto poweredAt=micros();
  // The submitter owns the source only until return. Rails can rise while it
  // packs the immutable target, but the scan cannot observe partial pixels.
  while(!_targetReady.load(std::memory_order_acquire))delay(1);
  enum EpdDrawError err;
  if (!_panelKnown) {
    err = epd_clear_checked();
    if (err == EPD_DRAW_SUCCESS) {
      const auto* waveform=_hl.waveform;
      auto firstMode=MODE_GC16;
      if(_jobGray4) {
        _hl.waveform=_cfg.readingWaveform;firstMode=MODE_GL16;
        if(_cfg.initOptions & EPD_LUT_4K)firstMode=static_cast<EpdDrawMode>(firstMode|MODE_GL4_COMPACT);
      }
      err=epd_hl_update_screen_from_white(&_hl,firstMode,temperature());
      _hl.waveform=waveform;
    }
  } else if (full) {
    const auto* waveform = _hl.waveform;
    if (_cfg.whiteRefreshWaveform) {
      _hl.waveform = _cfg.whiteRefreshWaveform;
      mode = _cfg.whiteRefreshMode;
    }
    err = epd_hl_update_screen_full(&_hl, mode, temperature());
    _hl.waveform = waveform;
  } else {
    const auto* waveform=_hl.waveform;
    if(_jobGray4){_hl.waveform=_cfg.readingWaveform;
      if(_cfg.initOptions & EPD_LUT_4K)mode=static_cast<EpdDrawMode>(mode|MODE_GL4_COMPACT);
    }
    err = epd_hl_update_screen(&_hl, mode, temperature());
    _hl.waveform=waveform;
  }
  if(err!=EPD_DRAW_SUCCESS && _jobGray4) {
    // Disable the optional waveform for this boot, and recover the immutable
    // original 16-gray candidate once. Never replay a partial optical baseline.
    _gray4Disabled=true;_metrics.recovered=1;
    memcpy(_hl.front_fb,_rawTarget,size_t(geometry().width)*geometry().height/2);
    err=epd_clear_checked();
    if(err==EPD_DRAW_SUCCESS)err=epd_hl_update_screen_from_white(&_hl,MODE_GC16,temperature());
    full=true;
  }
  const auto updatedAt=micros();
  epd_poweroff();
  _metrics.power_us=(poweredAt-workerAt)+(micros()-updatedAt);
  const auto scan=epd_metrics_get();
  _metrics.cycle_us=scan.cycle_us;
  _metrics.scan_us=scan.scan_us;
  _metrics.lut_us=scan.lut_us;
  _metrics.feed_wait_us=scan.feed_wait_us;
  _metrics.start_us=scan.start_us;_metrics.frame_wait_us=scan.frame_wait_us;_metrics.pace_us=scan.pace_us;
  _metrics.feed0_us=scan.feed0_us;_metrics.feed1_us=scan.feed1_us;
  _metrics.phase_avg_us=scan.phase_avg_us;
  _metrics.phase_min_us=scan.phase_min_us;
  _metrics.phase_max_us=scan.phase_max_us;
  _metrics.eof_gap_us=scan.eof_gap_us;
  _metrics.eof_work_us=scan.eof_work_us;
  _metrics.phases=scan.phases;
  _metrics.completed=scan.completed;
  _metrics.faults=scan.faults;
  _metrics.fault_phase=scan.fault_phase;
  _metrics.fault_line=scan.fault_line;
  _metrics.fault_thread=scan.fault_thread;

  _metrics.pixel_clock_hz=scan.pixel_clock_hz;
  _metrics.highlevel_us=(updatedAt-poweredAt)-scan.cycle_us;
  _finishedAt=micros();
  _metrics.worker_us=_finishedAt-workerAt;
  _metrics.clean=full || !_panelKnown;_metrics.reading=_jobReading;
  _metrics.gray_levels=_jobGray4 && !_metrics.recovered?4:16;
  // An interrupted waveform needs explicit resync before another scan.
  if(err==EPD_DRAW_SUCCESS){if(full || !_panelKnown)_readingTurns=0;else if(_jobReading)++_readingTurns;}
  _panelKnown = err == EPD_DRAW_SUCCESS;
  _faulted = !_panelKnown;
  _success.store(_panelKnown);
#if CONFIG_PM_ENABLE
  if (awake) { esp_pm_lock_release(awake); esp_pm_lock_delete(awake); }
#endif
  return _panelKnown;
}

void EpdiyDriver::prepare(const uint8_t* fb, RefreshMode mode) {
  const PanelGeometry g = geometry();
  auto* dst = reinterpret_cast<uint32_t*>(epd_hl_get_framebuffer(&_hl));
  for (uint32_t i = 0; i < g.bufferSize; ++i) dst[i] = gExpand[fb[i]];
  _lastBaseMode = modeFor(mode);
}
void EpdiyDriver::display(EpdBus& bus, const uint8_t* fb, const uint8_t*, RefreshMode mode, bool) {
  displayFinish(bus, nullptr);
  if (!_ready || !fb || _faulted) { _success.store(false); return; }
  _jobReading=_jobGray4=false;
  prepare(fb, mode);
  update(_lastBaseMode, mode != RefreshMode::Fast);
}
bool EpdiyDriver::displayStart(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool off) {
  if (!_worker) { display(bus, fb, prev, mode, off); return false; }
  displayFinish(bus, nullptr);
  if (!_ready || !fb || _faulted) { _success.store(false); return false; }
  // EPDiy already owns a full 4bpp target: copy before returning, never keep the
  // caller's framebuffer pointer or allocate a second per-refresh snapshot.
  _jobReading=_jobGray4=false;
  prepare(fb, mode); _jobFull = mode != RefreshMode::Fast;
  _queuedAt=micros(); _working.store(true); xTaskNotifyGive(_worker);
  return true;
}
bool EpdiyDriver::displayGray8Start(EpdBus& bus, const uint8_t* pixels, uint16_t stride, RefreshMode mode) {
  displayFinish(bus, nullptr);
  const auto g = geometry();
  if (!_ready || !pixels || stride < g.width || _faulted) { _success.store(false); return false; }
  _jobReading=mode==RefreshMode::Text || mode==RefreshMode::Text16 || mode==RefreshMode::TextClean;
  _jobFull=mode==RefreshMode::Full || mode==RefreshMode::Half || mode==RefreshMode::TextClean || (_jobReading && _readingTurns>=13);
  _jobGray4=(mode==RefreshMode::Text || mode==RefreshMode::TextStatus || mode==RefreshMode::TextClean) && _cfg.readingWaveform && _rawTarget && !_gray4Disabled && (!_jobFull || !_panelKnown);
  _lastBaseMode = _jobFull ? MODE_GC16 : MODE_GL16;
  if(_worker){_targetReady.store(false);_queuedAt=micros();_working.store(true);xTaskNotifyGive(_worker);}
  auto* dst = epd_hl_get_framebuffer(&_hl);
  for (uint16_t y = 0; y < g.height; ++y) {
    const auto* row = pixels + size_t(y)*stride;
    auto* output=dst+size_t(y)*g.width/2;
    uint16_t x=0;
    for (;x+8<=g.width;x+=8) {
      uint32_t a,b; memcpy(&a,row+x,4); memcpy(&b,row+x+4,4);
      const uint32_t packed=(a & b)==UINT32_MAX ? UINT32_MAX : uint32_t(gGrayNibble[a&255]) | (uint32_t(gGrayNibble[(a>>8)&255])<<4) |
        (uint32_t(gGrayNibble[(a>>16)&255])<<8) | (uint32_t(gGrayNibble[a>>24])<<12) |
        (uint32_t(gGrayNibble[b&255])<<16) | (uint32_t(gGrayNibble[(b>>8)&255])<<20) |
        (uint32_t(gGrayNibble[(b>>16)&255])<<24) | (uint32_t(gGrayNibble[b>>24])<<28);
      if(_jobGray4) {
        memcpy(_rawTarget+size_t(y)*g.width/2+x/2,&packed,4);
        const uint32_t quantized=((packed>>2)&0x33333333u)*5u;
        memcpy(output+x/2,&quantized,4);
      } else memcpy(output+x/2,&packed,4);
    }
    for (;x<g.width;x+=2) {
      const uint8_t packed=gGrayNibble[row[x]] | (gGrayNibble[row[x+1]]<<4);
      if(_jobGray4)_rawTarget[size_t(y)*g.width/2+x/2]=packed;
      output[x/2]=_jobGray4 ? uint8_t(((packed>>2)&0x33)*5) : packed;
    }
  }
  _targetReady.store(true,std::memory_order_release);
  if (!_worker) { update(_lastBaseMode, _jobFull); return false; }
  return true;
}
RefreshMetrics EpdiyDriver::lastRefreshMetrics() const {
  if (_working.load()) return {};
  auto result=_metrics;
  result.completion_us=_finishedAt ? uint32_t(micros()-_finishedAt) : 0;
  return result;
}
void EpdiyDriver::displayFinish(EpdBus&, const uint8_t*) {
  while (_working.load()) delay(1);
}

bool EpdiyDriver::ensureOverlayPlanes() {
  const auto bytes=geometry().bufferSize;
  auto allocate=[&](uint8_t*& plane) {
    if(plane)return true;
#if __has_include(<ResourceMemory.h>)
    plane=static_cast<uint8_t*>(resources::Memory::get().allocate(bytes,resources::Category::System,0,resources::Lifetime::System));
#else
    plane=static_cast<uint8_t*>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM));
#endif
    if(plane)memset(plane,0,bytes);return plane!=nullptr;
  };
  return allocate(_lsb) && allocate(_msb);
}

void EpdiyDriver::copyGrayscaleLsb(EpdBus& bus, const uint8_t* lsb) {
  displayFinish(bus, nullptr);
  if (ensureOverlayPlanes() && lsb) memcpy(_lsb, lsb, geometry().bufferSize);
}

void EpdiyDriver::copyGrayscaleMsb(EpdBus& bus, const uint8_t* msb) {
  displayFinish(bus, nullptr);
  if (ensureOverlayPlanes() && msb) memcpy(_msb, msb, geometry().bufferSize);
}

void EpdiyDriver::writeGrayscalePlaneStrip(EpdBus& bus, GrayPlane plane, const uint8_t* rows, uint16_t yStart,
                                           uint16_t numRows) {
  displayFinish(bus, nullptr);
  if(!ensureOverlayPlanes())return;
  uint8_t* dst = plane == GrayPlane::Lsb ? _lsb : _msb;
  if (!dst || !rows) return;
  const PanelGeometry g = geometry();
  if (yStart >= g.height) return;
  if (numRows > g.height - yStart) numRows = g.height - yStart;
  memcpy(dst + static_cast<uint32_t>(yStart) * g.widthBytes, rows, static_cast<size_t>(numRows) * g.widthBytes);
}

void EpdiyDriver::displayGray(EpdBus& bus, const uint8_t* fb, bool turnOff, const unsigned char* lut,
                              bool factoryMode) {
  displayFinish(bus, nullptr);
  (void)fb;  // the base page is already in EPDiy's framebuffer from display()
  (void)turnOff;
  (void)lut;
  (void)factoryMode;
  if (!_ready || !_lsb || !_msb) return;
  const PanelGeometry g = geometry();
  uint8_t* dst = epd_hl_get_framebuffer(&_hl);
  for (uint32_t i = 0; i < g.bufferSize; ++i) {
    const uint8_t l = _lsb[i], m = _msb[i];
    if ((l | m) == 0) continue;
    for (int bit = 0; bit < 8; ++bit) {
      const uint8_t mask = 0x80 >> bit;
      if (!((l | m) & mask)) continue;
      const uint8_t level = (l & mask) ? kDark : kLight;
      const uint32_t px = i * 8 + bit;
      uint8_t& cell = dst[px / 2];
      cell = (px & 1) ? static_cast<uint8_t>((cell & 0x0F) | (level << 4)) : static_cast<uint8_t>((cell & 0xF0) | level);
    }
  }
  // A clean (GC16) base keeps its mode; otherwise GL16 moves only the gray pixels.
  update(_lastBaseMode == MODE_GC16 ? MODE_GC16 : MODE_GL16, false);
}

PanelDriver& epdiyDriver() {
  static EpdiyDriver driver(FREEINK_EPDIY_CONFIG());
  return driver;
}

}  // namespace freeink

#endif  // FREEINK_DRIVER_EPDIY
