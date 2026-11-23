#include "src/driver/EpdiyDriver.h"
#include <future>
#include <cstdio>
namespace freeink {
const EpdiyConfig& testConfig() {
  static EpdBoardDefinition board;
  static EpdDisplay_t display;
  static EpdiyConfig config{&board, &display, nullptr, EPD_LUT_1K, 1200, 25, true};
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
    driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Fast, false);
    driver.displayFinish(bus, nullptr);
    assert(panel::lastKind == 2 && panel::lastMode == MODE_DU);
    panel::fail = true;
    driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Full, false);
    driver.displayFinish(bus, nullptr);
    assert(!driver.lastRefreshSucceeded() && panel::lastKind == 1);
    panel::fail = false;
    unsigned clears = panel::clears;
    driver.displayStart(bus, frame.data(), nullptr, RefreshMode::Fast, false);
    driver.displayFinish(bus, nullptr);
    assert(driver.lastRefreshSucceeded() && panel::clears == clears + 1);
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
    EpdiyDriver driver(config); driver.begin(bus);
    assert(!driver.supportsAsyncDisplay());
  }
  puts("EPDiy async ownership, completion, failure recovery, sleep and fallback passed");
}
