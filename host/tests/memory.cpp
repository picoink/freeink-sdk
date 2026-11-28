#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "Arduino.h"
#include "HostMemory.h"
#include "esp_heap_caps.h"
#include "ApplicationMemory.h"
#include "FontAlloc.h"
extern "C" void* c_allocation(size_t);
extern "C" void c_release(void*);
extern "C" void cpu_work();
int main() {
  using namespace freeink::host;
  Json config;
  assert(parseJson(R"({"memory":{"ram_bytes":4096,"psram_bytes":8192}})", config).empty());
  configureResources(config);
  firmwareAllocations = true;
  assert(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 8192);
  {
    auto application = freeink::memory::make<char[1024]>();
    assert(application && (*application)[0] == 0);
    (*application)[1023] = 42;
    assert(host_heap_free(MALLOC_CAP_INTERNAL) == 4096);
    assert(host_heap_free(MALLOC_CAP_SPIRAM) < 8192);
    assert(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 8192);
    assert(!freeink::memory::make<char[9000]>());
    assert(host_heap_free(MALLOC_CAP_INTERNAL) == 4096);
    assert((*application)[1023] == 42);
  }
  assert(host_heap_free(MALLOC_CAP_SPIRAM) == 8192);
  {
    auto* font = static_cast<char*>(fiFontMalloc(128));
    assert(font);
    font[0] = 42;
    void* occupied = ps_malloc(7500);
    assert(occupied);
    assert(!fiFontMalloc(1024));
    assert(!fiFontRealloc(font, 1024));
    assert(font[0] == 42 && host_heap_free(MALLOC_CAP_INTERNAL) == 4096);
    free(occupied);
    fiFontFree(font);
  }
  auto* c = static_cast<char*>(c_allocation(200));
  assert(c);
  memset(c, 7, 200);
  assert(host_heap_free(MALLOC_CAP_INTERNAL) < 4096);
  c_release(c);
  assert(host_heap_free(MALLOC_CAP_INTERNAL) == 4096);
  {
    std::vector<int> v(64, 3);
    assert(host_heap_free(MALLOC_CAP_INTERNAL) < 4096);
    assert(v[63] == 3);
  }
  assert(host_heap_free(MALLOC_CAP_INTERNAL) == 4096);
  auto* p = static_cast<char*>(heap_caps_malloc(512, MALLOC_CAP_INTERNAL));
  assert(p);
  memset(p, 42, 512);
  assert(!heap_caps_realloc(p, 9000, MALLOC_CAP_INTERNAL));
  assert(p[511] == 42);
  free(p);
  assert(host_heap_free(MALLOC_CAP_INTERNAL) == 4096);
  auto* external = ps_malloc(5000);
  assert(external);
  assert(host_heap_free(MALLOC_CAP_SPIRAM) < 8192);
  free(external);
  assert(host_heap_free(MALLOC_CAP_SPIRAM) == 8192);
  assert(!heap_caps_malloc(10, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM));
  void* blocks[4];
  for (auto& block : blocks) {
    block = heap_caps_malloc(900, MALLOC_CAP_INTERNAL);
    assert(block);
  }
  free(blocks[0]);
  free(blocks[2]);
  assert(host_heap_free(MALLOC_CAP_INTERNAL) > 1400);
  assert(host_heap_largest(MALLOC_CAP_INTERNAL) < 1400);
  assert(!heap_caps_malloc(1400, MALLOC_CAP_INTERNAL));
  free(blocks[1]);
  free(blocks[3]);
  assert(host_heap_largest(MALLOC_CAP_INTERNAL) == 4096);
  firmwareAllocations = false;
  Json failure;
  assert(parseJson(R"({"after":1,"pool":"psram"})", failure).empty());
  failAllocation(failure);
  assert(!heap_caps_malloc(64, MALLOC_CAP_SPIRAM));
  auto* retry = heap_caps_malloc(64, MALLOC_CAP_SPIRAM);
  assert(retry);
  heap_caps_free(retry);
  failAllocation(failure);
  firmwareAllocations = true;
  assert(!c_allocation(5000));
  firmwareAllocations = false;
  auto stats = resourceStats();
  assert(num(field(stats, "failures")) >= 4);
  assert(parseJson(R"({"memory":{"ram_bytes":4096,"psram_bytes":8388608}})", config).empty());
  configureResources(config);
  assert(parseJson(R"({"cpu":{"slowdown":3}})", config).empty());
  configureResources(config);
  firmwareAllocations = true;
  cpu_work();
  firmwareAllocations = false;
  stats = resourceStats();
  assert(num(field(stats, "cpu_wait_ms")) > 0);
  std::cout << stats.serialize() << '\n';
  std::cout << "C/C++ allocation interception, two heaps, fragmentation, failure preservation and CPU "
               "throttle passed\n";
}
