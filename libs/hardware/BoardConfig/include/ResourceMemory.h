#pragma once
#include <esp_heap_caps.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <memory>
#include <new>
#include <utility>

namespace freeink::resources {
enum class Category : uint8_t { System, Application, Document, Font, Database, Frame, Transient, Count };
enum class Lifetime : uint8_t { System, Application, Session, Operation, Cache };
enum class Failure : uint8_t { None, CategoryLimit, GlobalReserve, Heap, StaleOwner, ForeignOwner, Overflow, Count };
using Owner = uint64_t;
inline thread_local Owner currentOwner=0;
class OwnerScope {
 public:
  explicit OwnerScope(Owner owner):previous_(currentOwner){currentOwner=owner;}
  ~OwnerScope(){currentOwner=previous_;}
  OwnerScope(const OwnerScope&)=delete;
  OwnerScope& operator=(const OwnerScope&)=delete;
 private:
  Owner previous_;
};
constexpr size_t MiB = 1024 * 1024;
constexpr size_t reserveBytes = 2 * MiB;
constexpr uint32_t pressureFor(Category category) {return uint32_t(1)<<size_t(category);}
constexpr uint32_t globalPressure = (uint32_t(1)<<size_t(Category::Count))-1;
constexpr std::array<size_t, 7> limits{7*MiB/2,3*MiB/2,2*MiB,3*MiB,3*MiB/2,2*MiB,MiB/2};
constexpr const char* names[]{"system","application","document","font","database","frame","transient"};
struct Usage { size_t current=0, peak=0, allocations=0, failures=0, cached=0; };
struct OwnerUsage {Owner token=0;size_t current=0,peak=0;bool active=false;};
struct Snapshot {
  std::array<Usage,5> lifetimes{};
  std::array<OwnerUsage,32> owners{};
  std::array<Usage,7> categories{};
  std::array<size_t,size_t(Failure::Count)> failures{};
  size_t current=0,peak=0,capacity=0,free=0,unclassified=0;
};
// This ledger performs no dynamic allocation, eviction or heap walk. Explicit
// category totals include headers and a conservative heap-bookkeeping allowance.
// Physical free-space admission also covers allocations outside this ledger.
class Memory {
 public:
  static Memory& get() {
    // Global firmware buffers outlive setup()-constructed function statics on
    // host shutdown. Keep this allocation-free ledger alive through teardown.
    alignas(Memory) static unsigned char storage[sizeof(Memory)];
    static Memory* instance=new(storage) Memory;
    return *instance;
  }
  Owner beginOwner(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!id || generation_==UINT32_MAX) return 0;
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
  size_t reclaimOwner(Owner token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* owner=find(token);if(!owner)return 0;
    owner->active=false;const auto bytes=owner->bytes;
    while(owner->head)releaseLocked(owner->head);
    return bytes;
  }
  void* allocate(size_t size,Category category,Owner owner=currentOwner,Lifetime lifetime=Lifetime::Operation) {
    return reallocate(nullptr,size,category,owner,lifetime);
  }
  void* reallocate(void* pointer,size_t size,Category category=Category::Application,Owner owner=currentOwner,Lifetime lifetime=Lifetime::Application) {
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
    // Optional covers must leave half the transient allowance for active I/O.
    if(category==Category::Transient && lifetime==Lifetime::Cache && growth>MiB/4-std::min(MiB/4,usage.cached)) {
      fail(category,Failure::CategoryLimit);return nullptr;
    }
    const size_t capacity=heap_caps_get_total_size(caps),free=heap_caps_get_free_size(caps);
    const size_t global=capacity>reserveBytes?capacity-reserveBytes:0;
    // The category charges only growth. A physical realloc may still move
    // the block before freeing its predecessor; preserve reserve even during
    // that copy. Shrinking is in-place and needs no additional admission.
    const size_t physicalGrowth=old && growth?charge:growth;
    if (growth && (stats_.current>global || growth>global-stats_.current || free<reserveBytes || physicalGrowth>free-reserveBytes)) {
      fail(category,Failure::GlobalReserve);return nullptr;
    }
    // heap_caps_realloc preserves the original allocation on failure. Shrinking
    // never needs another full quota reservation, nor may it fall back to SRAM.
    Header* before=old?old->before:nullptr;
    Header* after=old?old->after:nullptr;
    auto* next=static_cast<Header*>(heap_caps_realloc(old,size+sizeof(Header),caps));
    if (!next) {fail(category,Failure::Heap);return nullptr;}
    *next=Header{size,charge,owner,category,lifetime,before,after};
    if(account) {
      if(!pointer){next->after=account->head;if(account->head)account->head->before=next;account->head=next;}
      else {if(before)before->after=next;else account->head=next;if(after)after->before=next;}
    }
    usage.current=usage.current-previous+charge;usage.peak=std::max(usage.peak,usage.current);
    if(lifetime==Lifetime::Cache)usage.cached=usage.cached-previous+charge;
    if (!pointer) ++usage.allocations;
    stats_.current=stats_.current-previous+charge;stats_.peak=std::max(stats_.peak,stats_.current);
    auto& life=stats_.lifetimes[size_t(lifetime)];
    life.current=life.current-previous+charge;life.peak=std::max(life.peak,life.current);if(!pointer)++life.allocations;
    if (account) {account->bytes=account->bytes-previous+charge;account->peak=std::max(account->peak,account->bytes);}
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
    for(size_t i=0;i<owners_.size();++i){const auto& o=owners_[i];s.owners[i]={o.token,o.bytes,o.peak,o.active};}
    s.capacity=heap_caps_get_total_size(caps);s.free=heap_caps_get_free_size(caps);
    const size_t physical=s.capacity-std::min(s.capacity,s.free);
    s.unclassified=physical>stats_.current?physical-stats_.current:0;return s;
  }
  // Driver-owned buffers are established before applications start and live
  // until reset. Attribute them without allocating or charging a second copy.
  // This hook is firmware-only and is not exported to an ELF application.
  bool accountPermanent(const void* pointer,size_t bytes) {
    if(!pointer || !bytes)return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for(const auto& p:permanent_)if(p==pointer)return true;
    for(auto& p:permanent_)if(!p) {
      auto& usage=stats_.categories[size_t(Category::System)];
      if(bytes>limits[size_t(Category::System)]-usage.current){fail(Category::System,Failure::CategoryLimit);return false;}
      p=pointer;usage.current+=bytes;usage.peak=std::max(usage.peak,usage.current);++usage.allocations;
      stats_.current+=bytes;stats_.peak=std::max(stats_.peak,stats_.current);
      auto& life=stats_.lifetimes[size_t(Lifetime::System)];life.current+=bytes;life.peak=std::max(life.peak,life.current);++life.allocations;
      return true;
    }
    return false;
  }
  uint32_t takePressure() {std::lock_guard<std::mutex> lock(mutex_);const auto value=pressure_;pressure_=0;return value;}
 private:
  static constexpr unsigned caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
  static constexpr size_t heapOverhead=32;
  struct alignas(std::max_align_t) Header {size_t size,charge;Owner owner;Category category;Lifetime lifetime;Header *before=nullptr,*after=nullptr;};
  struct OwnerAccount {Owner token=0;size_t bytes=0;bool active=false;Header* head=nullptr;size_t peak=0;};
  OwnerAccount* find(Owner token) {if(token)for(auto& o:owners_)if(o.token==token)return &o;return nullptr;}
  void fail(Category category,Failure failure) {
    ++stats_.categories[size_t(category)].failures;++stats_.failures[size_t(failure)];
    // A local quota refusal cannot be repaired by evicting another category.
    // Only physical exhaustion/fragmentation asks every cache to reclaim.
    if(failure==Failure::CategoryLimit)pressure_|=pressureFor(category);
    else if(failure==Failure::GlobalReserve || failure==Failure::Heap)pressure_|=globalPressure;
  }
  void releaseLocked(Header* h) {
    if (!h) return;
    auto& usage=stats_.categories[size_t(h->category)];usage.current-=h->charge;--usage.allocations;stats_.current-=h->charge;
    if(h->lifetime==Lifetime::Cache)usage.cached-=h->charge;
    auto& life=stats_.lifetimes[size_t(h->lifetime)];life.current-=h->charge;--life.allocations;
    if (auto* o=find(h->owner)) {
      o->bytes-=h->charge;
      if(h->before)h->before->after=h->after;else o->head=h->after;
      if(h->after)h->after->before=h->before;
    }
    heap_caps_free(h);
  }
  std::mutex mutex_;
  Snapshot stats_{};
  std::array<OwnerAccount,32> owners_{};
  std::array<const void*,16> permanent_{};
  uint32_t generation_=0;
  uint32_t pressure_=0;
};
inline void release(void* pointer) {Memory::get().releaseOwned(pointer);}
template<class T> struct Destroy {
  void operator()(T* pointer) const {if(pointer){std::destroy_at(pointer);release(pointer);}}
};
template<class T> using Pointer=std::unique_ptr<T,Destroy<T>>;
template<class T,class... Args> Pointer<T> make(Category category,Args&&... args) {
  auto* storage=Memory::get().allocate(sizeof(T),category);
  if(!storage)return {};
  new(storage) T{std::forward<Args>(args)...};
  return Pointer<T>(static_cast<T*>(storage));
}
template<class T> using Buffer=std::unique_ptr<T,decltype(&release)>;
// Cache-line-aligned external storage (for loaded executable sections). The
// alignment padding remains charged and must use releaseAligned, not free.
inline void* allocateAligned(size_t size,size_t alignment,Category category,Owner owner=currentOwner,Lifetime lifetime=Lifetime::Application) {
  if(alignment<alignof(void*) || (alignment&(alignment-1)) || alignment>4096 || size>SIZE_MAX-alignment-sizeof(void*))return nullptr;
  auto* raw=Memory::get().allocate(size+alignment+sizeof(void*),category,owner,lifetime);
  if(!raw)return nullptr;
  const auto address=(reinterpret_cast<uintptr_t>(raw)+sizeof(void*)+alignment-1)&~uintptr_t(alignment-1);
  memcpy(reinterpret_cast<void*>(address-sizeof(void*)),&raw,sizeof(raw));return reinterpret_cast<void*>(address);
}
inline void releaseAligned(void* pointer) {
  if(!pointer)return;
  void* raw=nullptr;memcpy(&raw,static_cast<unsigned char*>(pointer)-sizeof(void*),sizeof(raw));release(raw);
}
}  // namespace freeink::resources
