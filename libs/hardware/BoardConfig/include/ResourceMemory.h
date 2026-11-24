#pragma once
#include <esp_heap_caps.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>

namespace freeink::resources {
enum class Category : uint8_t { System, Application, Document, Font, Database, Frame, Transient, Count };
enum class Lifetime : uint8_t { System, Application, Session, Operation, Cache };
enum class Failure : uint8_t { None, CategoryLimit, GlobalReserve, Heap, StaleOwner, ForeignOwner, Overflow, Count };
using Owner = uint64_t;
constexpr size_t MiB = 1024 * 1024;
constexpr size_t reserveBytes = 2 * MiB;
constexpr std::array<size_t, 7> limits{7*MiB/2,3*MiB/2,2*MiB,3*MiB,3*MiB/2,2*MiB,MiB/2};
constexpr const char* names[]{"system","application","document","font","database","frame","transient"};
struct Usage { size_t current=0, peak=0, allocations=0, failures=0; };
struct Snapshot {
  std::array<Usage,7> categories{};
  std::array<size_t,size_t(Failure::Count)> failures{};
  size_t current=0,peak=0,capacity=0,free=0,unclassified=0;
};
// This ledger performs no dynamic allocation, eviction or heap walk. Explicit
// category totals include headers and a conservative heap-bookkeeping allowance.
// Physical free-space admission also covers allocations outside this ledger.
class Memory {
 public:
  static Memory& get() { static Memory instance; return instance; }
  Owner beginOwner(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!id) return 0;
    for (auto& o:owners_) if (!o.active && !o.bytes) {
      o={uint64_t(++generation_)<<32|id,0,true};return o.token;
    }
    return 0;
  }
  size_t endOwner(Owner token) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& o:owners_) if (o.token==token) {o.active=false;return o.bytes;}
    return 0;
  }
  size_t ownerBytes(Owner token) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& o:owners_) if (o.token==token) return o.bytes;
    return 0;
  }
  void* allocate(size_t size,Category category,Owner owner=0,Lifetime lifetime=Lifetime::Operation) {
    return reallocate(nullptr,size,category,owner,lifetime);
  }
  void* reallocate(void* pointer,size_t size,Category category=Category::Application,Owner owner=0,Lifetime lifetime=Lifetime::Application) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* old=pointer?static_cast<Header*>(pointer)-1:nullptr;
    if (old && old->owner!=owner) {fail(old->category,Failure::ForeignOwner);return nullptr;}
    if (!size) {releaseLocked(old);return nullptr;}
    if (old) {category=old->category;lifetime=old->lifetime;}
    if (size_t(category)>=limits.size()) return nullptr;
    auto* account=find(owner);
    if (owner && (!account || !account->active)) {fail(category,Failure::StaleOwner);return nullptr;}
    if (size>std::numeric_limits<size_t>::max()-sizeof(Header)-heapOverhead) {fail(category,Failure::Overflow);return nullptr;}
    const size_t charge=size+sizeof(Header)+heapOverhead, previous=old?old->charge:0;
    const size_t growth=charge>previous?charge-previous:0;
    auto& usage=stats_.categories[size_t(category)];
    if (growth>limits[size_t(category)]-usage.current) {fail(category,Failure::CategoryLimit);return nullptr;}
    const size_t capacity=heap_caps_get_total_size(caps),free=heap_caps_get_free_size(caps);
    const size_t global=capacity>reserveBytes?capacity-reserveBytes:0;
    if (growth && (stats_.current>global || growth>global-stats_.current || free<reserveBytes || growth>free-reserveBytes)) {
      fail(category,Failure::GlobalReserve);return nullptr;
    }
    // heap_caps_realloc preserves the original allocation on failure. Shrinking
    // never needs another full quota reservation, nor may it fall back to SRAM.
    auto* next=static_cast<Header*>(heap_caps_realloc(old,size+sizeof(Header),caps));
    if (!next) {fail(category,Failure::Heap);return nullptr;}
    *next=Header{size,charge,owner,category,lifetime};
    usage.current=usage.current-previous+charge;usage.peak=std::max(usage.peak,usage.current);
    if (!pointer) ++usage.allocations;
    stats_.current=stats_.current-previous+charge;stats_.peak=std::max(stats_.peak,stats_.current);
    if (account) account->bytes=account->bytes-previous+charge;
    return next+1;
  }
  bool release(void* pointer,Owner owner=0) {
    if (!pointer) return true;
    std::lock_guard<std::mutex> lock(mutex_);
    auto* header=static_cast<Header*>(pointer)-1;
    if (header->owner!=owner) {fail(header->category,Failure::ForeignOwner);return false;}
    releaseLocked(header);return true;
  }
  // Firmware RAII may release resources after their owner has been retired.
  // This entry point is never exported to an application.
  void releaseOwned(void* pointer) {
    if (!pointer) return;
    std::lock_guard<std::mutex> lock(mutex_);releaseLocked(static_cast<Header*>(pointer)-1);
  }
  size_t size(void* pointer) const { return pointer?(static_cast<Header*>(pointer)-1)->size:0; }
  Snapshot snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);auto s=stats_;
    s.capacity=heap_caps_get_total_size(caps);s.free=heap_caps_get_free_size(caps);
    const size_t physical=s.capacity-std::min(s.capacity,s.free);
    s.unclassified=physical>stats_.current?physical-stats_.current:0;return s;
  }
 private:
  static constexpr unsigned caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
  static constexpr size_t heapOverhead=32;
  struct alignas(std::max_align_t) Header {size_t size,charge;Owner owner;Category category;Lifetime lifetime;};
  struct OwnerAccount {Owner token=0;size_t bytes=0;bool active=false;};
  OwnerAccount* find(Owner token) {if(token)for(auto& o:owners_)if(o.token==token)return &o;return nullptr;}
  void fail(Category category,Failure failure) {++stats_.categories[size_t(category)].failures;++stats_.failures[size_t(failure)];}
  void releaseLocked(Header* h) {
    if (!h) return;
    auto& usage=stats_.categories[size_t(h->category)];usage.current-=h->charge;--usage.allocations;stats_.current-=h->charge;
    if (auto* o=find(h->owner)) o->bytes-=h->charge;
    heap_caps_free(h);
  }
  std::mutex mutex_;
  Snapshot stats_{};
  std::array<OwnerAccount,32> owners_{};
  uint32_t generation_=0;
};
inline void release(void* pointer) {Memory::get().releaseOwned(pointer);}
}  // namespace freeink::resources
