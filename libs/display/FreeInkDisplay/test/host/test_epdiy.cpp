#include "src/driver/EpdiyDriver.h"
#include <future>
#include <cstdio>
namespace freeink {
const EpdiyConfig& testConfig() {
  static EpdBoardDefinition board;
  static EpdDisplay_t display;
  static EpdWaveform normal, white, reading;
  static EpdiyConfig config{&board, &display, &normal, EPD_LUT_1K, 1200, 25, true, &white, MODE_GL16, &reading};
  return config;
}
}
int main() {
  using namespace freeink;
  EpdBus bus;
  std::array<uint8_t, 16> frame{};
  {
    EpdiyDriver driver(testConfig()); driver.begin(bus);
    assert(driver.supportsAsyncDisplay());
    panel::block(); frame.fill(0x80);
    assert(driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Fast, false));
    panel::waitEntered(); assert(driver.refreshBusy(bus));
    frame.fill(0xff);
    assert(panel::framebuffer[0] == 0x0f && panel::framebuffer[1] == 0);
    panel::release(); driver.displayFinish(bus, nullptr);
    assert(!driver.refreshBusy(bus) && driver.lastRefreshSucceeded());
    assert(panel::lastKind == 0 && panel::lastMode == MODE_GC16);
    assert(panel::lastWaveform == testConfig().waveform);
    driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Fast, false);
    driver.displayFinish(bus, nullptr);
    assert(panel::lastKind == 2 && panel::lastMode == MODE_DU);
    std::array<uint8_t,128> gray{};
    for (size_t i=0;i<gray.size();++i) gray[i] = (i%16)*17;
    panel::block();
    assert(driver.displayGray8Start(bus,gray.data(),16,RefreshMode::Fast));
    panel::waitEntered(); gray.fill(255);
    for (size_t i=0;i<64;++i) assert(panel::framebuffer[i] == uint8_t((i*2%16) | ((i*2+1)%16)<<4));
    panel::release(); driver.displayFinish(bus,nullptr);
    assert(panel::lastMode == MODE_GL16 && driver.lastRefreshSucceeded());
    for (unsigned base=0;base<256;base+=128) {
      for (unsigned i=0;i<gray.size();++i) gray[i]=uint8_t(base+i);
      driver.displayGray8Start(bus,gray.data(),16,RefreshMode::Fast);
      driver.displayFinish(bus,nullptr);
      for (unsigned i=0;i<64;++i)
        assert(panel::framebuffer[i]==uint8_t(((base+2*i+8)/17) | (((base+2*i+9)/17)<<4)));
    }
    for(unsigned page=0;page<14;++page) {
      for(unsigned i=0;i<gray.size();++i)gray[i]=uint8_t(i*2);
      if(page%2)std::fill(gray.begin(),gray.begin()+32,255);
      const auto original=gray;
      driver.displayGray8Start(bus,gray.data(),16,RefreshMode::Text);driver.displayFinish(bus,nullptr);
      assert(gray==original);
      if(page==13)assert(panel::lastKind==1 && panel::lastWaveform==testConfig().whiteRefreshWaveform);
      else {
        assert(panel::lastKind==2 && panel::lastWaveform==testConfig().readingWaveform);
        for(unsigned i=0;i<64;++i)assert(panel::framebuffer[i]==uint8_t(5*(((original[2*i]+8)/17)>>2) | (5*(((original[2*i+1]+8)/17)>>2)<<4)));
        // Footer repaint does not consume the body-cleaning cadence.
        driver.displayGray8Start(bus,gray.data(),16,RefreshMode::TextStatus);driver.displayFinish(bus,nullptr);
        assert(panel::lastKind==2 && panel::lastWaveform==testConfig().readingWaveform);
      }
    }
    const unsigned knownClears = panel::clears;
    driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Full, false);
    driver.displayFinish(bus, nullptr);
    assert(driver.lastRefreshSucceeded() && panel::lastKind == 1 && panel::lastMode == MODE_GL16);
    assert(panel::clears == knownClears && panel::lastWaveform == testConfig().whiteRefreshWaveform);
    driver.displayGray8Start(bus,gray.data(),16,RefreshMode::Fast);
    driver.displayFinish(bus,nullptr);
    assert(panel::lastKind == 2 && panel::lastWaveform == testConfig().waveform);
    panel::fail = true;
    driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Full, false);
    driver.displayFinish(bus, nullptr);
    assert(!driver.lastRefreshSucceeded() && panel::lastKind == 1);
    const unsigned failedCalls=panel::calls, failedClears=panel::clears;
    for (int retry=0;retry<5;++retry) {
      driver.displayStart(bus,frame.data(),nullptr,RefreshMode::Full,false);
      driver.displayFinish(bus,nullptr);
      assert(!driver.lastRefreshSucceeded());
      assert(panel::calls==failedCalls && panel::clears==failedClears);
    }
    panel::fail = false;
    driver.requestResync();
    unsigned clears = panel::clears;
    driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Fast, false);
    driver.displayFinish(bus, nullptr);
    assert(driver.lastRefreshSucceeded() && panel::clears == clears + 1);
    assert(panel::lastWaveform == testConfig().waveform && panel::lastMode == MODE_GC16);
    panel::block();
    driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Full, false);
    panel::waitEntered();
    auto sleeping = std::async(std::launch::async, [&] { driver.deepSleep(bus); });
    assert(sleeping.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    assert(panel::deinits == 0);
    panel::release(); sleeping.get();
    assert(!driver.refreshBusy(bus) && !driver.supportsAsyncDisplay() && panel::deinits == 1);
    driver.begin(bus);
    assert(driver.supportsAsyncDisplay() && panel::inits == 2);
  }
  {
    EpdiyDriver driver(testConfig());driver.begin(bus);
    std::array<uint8_t,128> gray{};
    for(unsigned i=0;i<gray.size();++i)gray[i]=uint8_t(i*2);
    const auto original=gray;const unsigned clears=panel::clears;
    driver.displayGray8Start(bus,gray.data(),16,RefreshMode::TextClean);driver.displayFinish(bus,nullptr);
    assert(gray==original && panel::clears==clears+1);
    assert(panel::lastKind==0 && panel::lastMode==MODE_GL16);
    assert(panel::lastWaveform==testConfig().readingWaveform);
    assert(driver.lastRefreshMetrics().clean && driver.lastRefreshMetrics().gray_levels==4);
    driver.displayGray8Start(bus,gray.data(),16,RefreshMode::TextClean);driver.displayFinish(bus,nullptr);
    assert(panel::clears==clears+1 && panel::lastKind==1);
    assert(panel::lastWaveform==testConfig().whiteRefreshWaveform && gray==original);
  }
  joinTasks();
  failTaskAllocation = true;
  {
    EpdiyDriver driver(testConfig()); driver.begin(bus);
    assert(!driver.supportsAsyncDisplay());
    unsigned calls = panel::calls;
    assert(!driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Full, false));
    assert(panel::calls == calls + 1 && driver.lastRefreshSucceeded());
  }
  failTaskAllocation = false;
  {
    EpdiyConfig config = testConfig(); config.deferredRefresh = false;
    config.whiteRefreshWaveform = nullptr;
    EpdiyDriver driver(config); driver.begin(bus);
    assert(!driver.supportsAsyncDisplay());
    driver.display(bus,frame.data(),nullptr,RefreshMode::Full,false);
    driver.display(bus,frame.data(),nullptr,RefreshMode::Full,false);
    assert(panel::lastKind == 1 && panel::lastMode == MODE_GC16 && panel::lastWaveform == config.waveform);
  }
  puts("EPDiy async ownership, completion, failure recovery, sleep and fallback passed");
}
