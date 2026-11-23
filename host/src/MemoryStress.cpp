#include <time.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <thread>

#include "HostMemory.h"
#include "esp_heap_caps.h"
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void __real_free(void*);
#endif
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define POISON(p, n) __asan_poison_memory_region(p, n)
#define UNPOISON(p, n) __asan_unpoison_memory_region(p, n)
#else
#define POISON(p, n) ((void)0)
#define UNPOISON(p, n) ((void)0)
#endif
namespace {
void* rawMalloc(size_t n) {
#ifdef __APPLE__
  return malloc_zone_malloc(malloc_default_zone(), n);
#else
  return __real_malloc(n);
#endif
}
void rawFree(void* p) {
#ifdef __APPLE__
  if (p) malloc_zone_free(malloc_zone_from_ptr(p), p);
#else
  __real_free(p);
#endif
}
void* rawRealloc(void* p, size_t n) {
#ifdef __APPLE__
  return p ? malloc_zone_realloc(malloc_zone_from_ptr(p), p, n) : rawMalloc(n);
#else
  return __real_realloc(p, n);
#endif
}
thread_local bool inAllocator = false;
struct Guard {
  bool prior;
  Guard() : prior(inAllocator) { inAllocator = true; }
  ~Guard() { inAllocator = prior; }
};
struct Allocation {
  size_t offset, extent, size;
};
struct Pool {
  unsigned char* data = nullptr;
  size_t capacity = 0, reserved = 0, used = 0, peak = 0, minFree = 0;
  std::map<size_t, size_t> holes;
  std::map<void*, Allocation> live;
  bool contains(void* p) const {
    return data && uintptr_t(p) >= uintptr_t(data) && uintptr_t(p) < uintptr_t(data) + capacity;
  }
  void initialize(size_t bytes, size_t reserve) {
    if (!live.empty()) throw std::runtime_error("Cannot reconfigure a live resource heap");
    if (data) {
      UNPOISON(data, capacity);
      rawFree(data);
    }
    data = nullptr;
    holes.clear();
    capacity = bytes;
    reserved = reserve;
    used = peak = 0;
    minFree = bytes - reserve;
    if (bytes) {
      data = static_cast<unsigned char*>(rawMalloc(bytes));
      if (!data) throw std::bad_alloc();
      POISON(data, bytes);
      holes.emplace(reserve, bytes - reserve);
    }
  }
  size_t largest() const {
    size_t n = 0;
    for (const auto& h : holes) n = std::max(n, h.second);
    return n;
  }
  void hole(size_t offset, size_t size) {
    if (!size) return;
    auto after = holes.lower_bound(offset);
    if (after != holes.begin()) {
      auto before = std::prev(after);
      if (before->first + before->second == offset) {
        offset = before->first;
        size += before->second;
        holes.erase(before);
      }
    }
    after = holes.lower_bound(offset);
    if (after != holes.end() && offset + size == after->first) {
      size += after->second;
      holes.erase(after);
    }
    holes.emplace(offset, size);
  }
  void* allocate(size_t n, size_t alignment) {
    if (n > SIZE_MAX - 32) return nullptr;
    const size_t extent = ((std::max<size_t>(1, n) + 15) / 16) * 16 + 16;
    for (auto it = holes.begin(); it != holes.end(); ++it) {
      const size_t start = it->first, available = it->second;
      const size_t offset = ((uintptr_t(data) + start + alignment - 1) & ~(alignment - 1)) - uintptr_t(data);
      if (offset < start || offset - start > available || extent > available - (offset - start)) continue;
      holes.erase(it);
      hole(start, offset - start);
      hole(offset + extent, available - (offset - start) - extent);
      void* p = data + offset;
      live.emplace(p, Allocation{offset, extent, n});
      used += extent;
      peak = std::max(peak, used);
      minFree = std::min(minFree, capacity - reserved - used);
      UNPOISON(p, n);
      return p;
    }
    return nullptr;
  }
  void release(void* p) {
    auto it = live.find(p);
    if (it == live.end()) {
      std::fprintf(stderr, "[resources] Invalid or double free %p\n", p);
      std::abort();
    }
    const auto block = it->second;
    POISON(p, block.extent);
    used -= block.extent;
    live.erase(it);
    hole(block.offset, block.extent);
  }
};
struct Resources {
  std::mutex mutex;
  Pool ram, psram;
  bool enabled = false;
  double slowdown = 1, ioRate = 0, ioMs = 0;
  uint64_t attempts = 0, failures = 0, failAt = 0, minFailureSize = 0;
  std::string failPool, lastFailure;
  std::atomic<uint64_t> cpuSleepNs{0}, cpuNs{0}, ioNs{0}, ioBytes{0}, ioOps{0};
};
Resources& resources() {
  static Resources* r = new (rawMalloc(sizeof(Resources))) Resources;
  return *r;
}
Pool* owner(void* p) {
  auto& r = resources();
  return r.ram.contains(p) ? &r.ram : r.psram.contains(p) ? &r.psram : nullptr;
}
size_t sizeField(const freeink::host::Json& j, const char* name, size_t fallback) {
  auto& v = freeink::host::field(j, name);
  double n = freeink::host::num(v, double(fallback));
  if (!std::isfinite(n) || n < 0 || n > 1024.0 * 1024 * 1024 || n != std::floor(n))
    throw std::runtime_error(std::string("Invalid resource byte count: ") + name);
  return size_t(n);
}
void* allocate(size_t n, unsigned caps, size_t alignment = 16) {
  auto& r = resources();
  ++r.attempts;
  const bool external = (caps & MALLOC_CAP_SPIRAM) != 0,
             internal = (caps & (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)) != 0;
  if (external && internal) {
    ++r.failures;
    r.lastFailure = "incompatible capabilities";
    return nullptr;
  }
  if (r.failAt && r.attempts >= r.failAt && n >= r.minFailureSize &&
      (r.failPool.empty() ||
       ((external || (!internal && n > 4096)) ? r.failPool == "psram" : r.failPool == "ram"))) {
    r.failAt = 0;
    ++r.failures;
    r.lastFailure = "injected allocation failure";
    return nullptr;
  }
  void* result = nullptr;
  if (external)
    result = r.psram.allocate(n, alignment);
  else if (internal)
    result = r.ram.allocate(n, alignment);
  else if (n <= 4096) {
    result = r.ram.allocate(n, alignment);
    if (!result) result = r.psram.allocate(n, alignment);
  } else {
    result = r.psram.allocate(n, alignment);
    if (!result) result = r.ram.allocate(n, alignment);
  }
  if (!result) {
    ++r.failures;
    r.lastFailure = std::string(external   ? "psram"
                                : internal ? "ram"
                                           : "default") +
                    " allocation failed: " + std::to_string(n) + " bytes";
  }
  return result;
}
uint64_t threadNs() {
  timespec t{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  return uint64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}
}  // namespace
namespace freeink::host {
thread_local bool firmwareAllocations = false;
void configureResources(const Json& config) {
  Guard guard;
  HostScope scope;
  auto& r = resources();
  std::lock_guard<std::mutex> lock(r.mutex);
  for (const auto& p : config.get<Object>())
    if (p.first != "memory" && p.first != "cpu" && p.first != "sd" && p.first != "display")
      throw std::runtime_error("Unknown resource section: " + p.first);
    else if (!p.second.is<Object>())
      throw std::runtime_error("Resource section must be an object: " + p.first);
  const auto& memory = field(config, "memory");
  r.enabled = memory.is<Object>();
  if (r.enabled) {
    const size_t ram = sizeField(memory, "ram_bytes", 131072),
                 psram = sizeField(memory, "psram_bytes", 16777216),
                 rr = sizeField(memory, "ram_reserved", 0), pr = sizeField(memory, "psram_reserved", 0);
    if (rr > ram || pr > psram) throw std::runtime_error("Reserved memory exceeds capacity");
    r.ram.initialize(ram, rr);
    r.psram.initialize(psram, pr);
  }
  r.slowdown = num(field(field(config, "cpu"), "slowdown"), 1);
  r.ioRate = num(field(field(config, "sd"), "bytes_per_second"), 0);
  r.ioMs = num(field(field(config, "sd"), "operation_ms"), 0);
  if (!std::isfinite(r.slowdown) || r.slowdown < 1 || r.slowdown > 1000 || !std::isfinite(r.ioRate) ||
      r.ioRate < 0 || !std::isfinite(r.ioMs) || r.ioMs < 0 || r.ioMs > 60000)
    throw std::runtime_error("Invalid timing resource configuration");
  runtime().displayFullMs = sizeField(field(config, "display"), "full_ms", 0);
  runtime().displayFastMs = sizeField(field(config, "display"), "fast_ms", 0);
}
Json resourceStats() {
  Guard guard;
  HostScope scope;
  auto& r = resources();
  std::lock_guard<std::mutex> lock(r.mutex);
  auto describe = [](const Pool& p) {
    return Json(Object{{"capacity", number(p.capacity)},
                       {"reserved", number(p.reserved)},
                       {"used", number(p.used)},
                       {"peak", number(p.peak)},
                       {"free", number(p.capacity - p.reserved - p.used)},
                       {"minimum_free", number(p.minFree)},
                       {"largest_free_block", number(p.largest())},
                       {"allocations", number(p.live.size())}});
  };
  return Json(Object{{"enabled", Json(true)},
                     {"memory_enabled", Json(r.enabled)},
                     {"ram", describe(r.ram)},
                     {"psram", describe(r.psram)},
                     {"attempts", number(r.attempts)},
                     {"failures", number(r.failures)},
                     {"last_failure", Json(r.lastFailure)},
                     {"cpu_slowdown", number(r.slowdown)},
                     {"cpu_ms", number(r.cpuNs.load() / 1e6)},
                     {"cpu_wait_ms", number(r.cpuSleepNs.load() / 1e6)},
                     {"io_wait_ms", number(r.ioNs.load() / 1e6)},
                     {"io_operations", number(r.ioOps.load())},
                     {"io_bytes", number(r.ioBytes.load())}});
}
void failAllocation(const Json& settings) {
  Guard g;
  HostScope s;
  auto& r = resources();
  std::lock_guard<std::mutex> l(r.mutex);
  if (!r.enabled) throw std::runtime_error("Memory constraints are not enabled");
  r.failAt = r.attempts + sizeField(settings, "after", 1);
  r.minFailureSize = sizeField(settings, "minimum_bytes", 0);
  r.failPool = str(field(settings, "pool"));
  if (!r.failPool.empty() && r.failPool != "ram" && r.failPool != "psram")
    throw std::runtime_error("Unknown allocation pool");
}
void cpuCheckpoint() {
  if (!firmwareAllocations || inAllocator) return;
  static thread_local unsigned counter = 0;
  static thread_local uint64_t previous = 0;
  if ((++counter & 1023) != 0) return;
  Guard guard;
  auto& r = resources();
  const auto t = threadNs();
  if (!previous) {
    previous = t;
    return;
  }
  const auto elapsed = t - previous;
  if (elapsed < 1000000) return;
  r.cpuNs += elapsed;
  if (r.slowdown > 1) {
    const auto ns = uint64_t(elapsed * (r.slowdown - 1));
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
    r.cpuSleepNs += ns;
  }
  previous = threadNs();
}
void ioCost(size_t bytes) {
  Guard guard;
  auto& r = resources();
  r.ioOps++;
  r.ioBytes += bytes;
  const double ms = r.ioMs + (r.ioRate > 0 ? bytes * 1000.0 / r.ioRate : 0);
  if (ms > 0) {
    const auto ns = uint64_t(ms * 1e6);
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
    r.ioNs += ns;
  }
}
}  // namespace freeink::host
extern "C" {
void* host_caps_malloc(size_t n, unsigned caps) {
  if (inAllocator) return rawMalloc(n);
  Guard guard;
  auto& r = resources();
  if (!r.enabled) return rawMalloc(n);
  std::lock_guard<std::mutex> lock(r.mutex);
  return allocate(n, caps);
}
void* host_malloc(size_t n) {
  if (inAllocator || !freeink::host::firmwareAllocations) return rawMalloc(n);
  return host_caps_malloc(n, MALLOC_CAP_DEFAULT);
}
void* host_calloc(size_t n, size_t s) {
  if (s && n > SIZE_MAX / s) return nullptr;
  void* p = host_malloc(n * s);
  if (p) memset(p, 0, n * s);
  return p;
}
void host_free(void* p) {
  if (!p) return;
  if (inAllocator) {
    rawFree(p);
    return;
  }
  Guard guard;
  auto* pool = owner(p);
  if (!pool) {
    rawFree(p);
    return;
  }
  std::lock_guard<std::mutex> lock(resources().mutex);
  pool->release(p);
}
void* host_caps_realloc(void* p, size_t n, unsigned caps) {
  if (!p) return host_caps_malloc(n, caps);
  if (!n) {
    host_free(p);
    return nullptr;
  }
  if (inAllocator) return rawRealloc(p, n);
  Guard guard;
  auto* pool = owner(p);
  if (!pool) return rawRealloc(p, n);
  std::lock_guard<std::mutex> lock(resources().mutex);
  auto it = pool->live.find(p);
  if (it == pool->live.end()) std::abort();
  const auto old = it->second;
  void* q = allocate(n, caps);
  if (!q) return nullptr;
  memcpy(q, p, std::min(old.size, n));
  pool->release(p);
  return q;
}
void* host_realloc(void* p, size_t n) {
  if (inAllocator) return rawRealloc(p, n);
  if (!p) return host_malloc(n);
  return host_caps_realloc(p, n, MALLOC_CAP_DEFAULT);
}
void* host_aligned_alloc(size_t a, size_t n) {
  if (a < sizeof(void*) || (a & (a - 1))) return nullptr;
  if (inAllocator || !freeink::host::firmwareAllocations) {
    void* p = nullptr;
    return posix_memalign(&p, a, n) == 0 ? p : nullptr;
  }
  Guard guard;
  auto& r = resources();
  if (!r.enabled) {
    void* p = nullptr;
    return posix_memalign(&p, a, n) == 0 ? p : nullptr;
  }
  std::lock_guard<std::mutex> l(r.mutex);
  return allocate(n, MALLOC_CAP_DEFAULT, a);
}
size_t host_heap_free(unsigned c) {
  Guard g;
  auto& r = resources();
  if (!r.enabled) return 16 * 1024 * 1024;
  std::lock_guard<std::mutex> l(r.mutex);
  auto& p = (c & MALLOC_CAP_SPIRAM) ? r.psram : r.ram;
  return p.capacity - p.reserved - p.used;
}
size_t host_heap_total(unsigned c) {
  Guard g;
  auto& r = resources();
  if (!r.enabled) return 16 * 1024 * 1024;
  std::lock_guard<std::mutex> l(r.mutex);
  return (c & MALLOC_CAP_SPIRAM) ? r.psram.capacity : r.ram.capacity;
}
size_t host_heap_largest(unsigned c) {
  Guard g;
  auto& r = resources();
  if (!r.enabled) return 16 * 1024 * 1024;
  std::lock_guard<std::mutex> l(r.mutex);
  return ((c & MALLOC_CAP_SPIRAM) ? r.psram : r.ram).largest();
}
void __sanitizer_cov_trace_pc_guard(uint32_t*) { freeink::host::cpuCheckpoint(); }
void __sanitizer_cov_trace_pc_guard_init(uint32_t* start, uint32_t* end) {
  static uint32_t id = 0;
  for (auto p = start; p != end; ++p) *p = ++id;
}
#ifndef __APPLE__
void* __wrap_malloc(size_t n) { return host_malloc(n); }
void* __wrap_calloc(size_t n, size_t s) { return host_calloc(n, s); }
void* __wrap_realloc(void* p, size_t n) { return host_realloc(p, n); }
void __wrap_free(void* p) { host_free(p); }
#endif
}
// Mach-O references from the executable resolve to these definitions. Calls
// inside system dylibs retain their original allocator; our raw path uses zones.
#ifdef __APPLE__
extern "C" void* malloc(size_t n) { return host_malloc(n); }
extern "C" void* calloc(size_t n, size_t s) { return host_calloc(n, s); }
extern "C" void* realloc(void* p, size_t n) { return host_realloc(p, n); }
extern "C" void free(void* p) { host_free(p); }
#endif
void* operator new(size_t n) {
  if (auto* p = host_malloc(n)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept {
  try {
    return ::operator new(n);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return ::operator new(n, std::nothrow); }
void operator delete(void* p) noexcept { host_free(p); }
void operator delete[](void* p) noexcept { host_free(p); }
void operator delete(void* p, size_t) noexcept { host_free(p); }
void operator delete[](void* p, size_t) noexcept { host_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { host_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { host_free(p); }
void* operator new(size_t n, std::align_val_t a) {
  if (auto* p = host_aligned_alloc(size_t(a), n)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept { host_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { host_free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { host_free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { host_free(p); }
