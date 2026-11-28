#include "src/driver/EpdiyDriver.h"
#include <future>
#include <cstdio>
#include <vector>
#include <cmath>
static void calibrationPixels() {
  using namespace freeink;
  for(auto size: {std::pair<int,int>{16,8},{1216,684}}) {
    const auto [w,h]=size;const size_t bytes=size_t(w)*h/2;
    const std::vector<int> shifts=w==16?std::vector<int>{-40,-17,-16,-15,-4,-3,-2,-1,0,1,2,3,4,15,16,17,40}:std::vector<int>{-40,-3,0,3,40};
    std::vector<uint8_t> original(bytes),actual(bytes+2);
    for(size_t i=0;i<bytes;++i)original[i]=uint8_t(i*71+i/13);
    for(int dx:shifts)for(int dy:shifts) {
      actual.front()=0xa5;actual.back()=0x5a;std::copy(original.begin(),original.end(),actual.begin()+1);
      translateGray4(actual.data()+1,w,h,dx,dy);
      for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const int sx=x-dx,sy=y-dy;
        const uint8_t expected=sx<0 || sx>=w || sy<0 || sy>=h?15:(original[(sy*w+sx)/2]>>((sx&1)*4))&15;
        assert(((actual[1+(y*w+x)/2]>>((x&1)*4))&15)==expected);
      }
      assert(actual.front()==0xa5 && actual.back()==0x5a);
    }
  }
  DisplayCalibration c{{20,-20,-20,20}};assert(c.valid() && c.x()==40 && c.y()==-40);
  float x=(600.0f+c.nativeX())/1216,y=(300.0f+c.nativeY())/684;
  assert(c.unmap(x,y,1216,684) && std::abs(x*1216-600)<.001 && std::abs(y*684-300)<.001);
  x=1;y=1;assert(!c.unmap(x,y,1216,684));
  x=1;y=1;assert(c.unmap(x,y,1216,684,true) && x==1 && y==1);
  c.side[0]=21;assert(!c.valid());
}
namespace freeink {
const EpdiyConfig& testConfig() {
  static EpdBoardDefinition board;
  static EpdDisplay_t display;
  static EpdWaveform normal, full, reading;
  static EpdiyConfig config{&board, &display, &normal, EPD_LUT_1K, 1200, 25, true, &full, MODE_GC16, &reading};
  return config;
}
}
int main() {
  using namespace freeink;
  calibrationPixels();
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
      if(page==13)assert(panel::lastKind==1 && panel::lastMode==MODE_GC16 && panel::lastWaveform==testConfig().fullRefreshWaveform);
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
    assert(driver.lastRefreshSucceeded() && panel::lastKind == 1 && panel::lastMode == MODE_GC16);
    assert(panel::clears == knownClears && panel::lastWaveform == testConfig().fullRefreshWaveform);
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
    assert(panel::clears==clears+1 && panel::lastKind==1 && panel::lastMode==MODE_GC16);
    assert(panel::lastWaveform==testConfig().fullRefreshWaveform && gray==original);
  }
  {
    EpdiyDriver driver(testConfig());driver.begin(bus);
    DisplayCalibration offset{{3,0,0,1}};
    assert(driver.setCalibration(offset));
    std::array<uint8_t,128> gray{};for(unsigned i=0;i<gray.size();++i)gray[i]=uint8_t((i%16)*17);
    const auto original=gray;
    panel::block();driver.displayGray8Start(bus,gray.data(),16,RefreshMode::Text);panel::waitEntered();
    assert(!driver.setCalibration({}));panel::release();driver.displayFinish(bus,nullptr);
    assert(driver.lastRefreshMetrics().clean && gray==original);
    for(int y=0;y<8;++y)for(int x=0;x<16;++x) {
      const int sx=x+3,sy=y+1;
      const int expected=sx<16 && sy<8?sx:15;
      assert(((panel::framebuffer[(y*16+x)/2]>>((x&1)*4))&15)==expected);
    }
    driver.displayGray8Start(bus,gray.data(),16,RefreshMode::Text);driver.displayFinish(bus,nullptr);
    assert(driver.lastRefreshMetrics().gray_levels==4 && !driver.lastRefreshMetrics().clean && gray==original);
    offset.side[0]=21;assert(!driver.setCalibration(offset));
    assert(driver.setCalibration({}));driver.displayGray8Start(bus,gray.data(),16,RefreshMode::Fast);driver.displayFinish(bus,nullptr);
    assert(driver.lastRefreshMetrics().clean);
    for(unsigned i=0;i<64;++i)assert(panel::framebuffer[i]==uint8_t(2*i%16 | ((2*i+1)%16)<<4));
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
    config.fullRefreshWaveform = nullptr;
    EpdiyDriver driver(config); driver.begin(bus);
    assert(!driver.supportsAsyncDisplay());
    driver.display(bus,frame.data(),nullptr,RefreshMode::Full,false);
    driver.display(bus,frame.data(),nullptr,RefreshMode::Full,false);
    assert(panel::lastKind == 1 && panel::lastMode == MODE_GC16 && panel::lastWaveform == config.waveform);
  }
  puts("EPDiy async ownership, completion, failure recovery, sleep and fallback passed");
}
