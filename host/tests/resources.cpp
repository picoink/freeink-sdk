#include <ResourceMemory.h>
#include <HostRuntime.h>
#include <HostMemory.h>
#include <cassert>
#include <cstring>

int main() {
  using namespace freeink::resources;
  using namespace freeink::host;
  Json config;
  assert(parseJson(R"({"memory":{"ram_bytes":65536,"psram_bytes":16777216}})",config).empty());
  configureResources(config);
  auto& memory=Memory::get();
  const auto owner=memory.beginOwner(1),other=memory.beginOwner(2);
  assert(owner && other && owner!=other);
  auto* db=static_cast<unsigned char*>(memory.allocate(MiB,Category::Database,owner,Lifetime::Session));
  assert(db && !(uintptr_t(db)%alignof(std::max_align_t)));
  memset(db,42,MiB);
  // A font cannot consume the independent database quota.
  auto* font=memory.allocate(3*MiB-256,Category::Font,other,Lifetime::Cache);assert(font);
  assert(!memory.allocate(512,Category::Font,other));
  db=static_cast<unsigned char*>(memory.reallocate(db,5*MiB/4,Category::Database,owner));
  assert(db && db[MiB-1]==42); // Delta fits; two complete copies would not.
  auto before=memory.snapshot();
  assert(!memory.reallocate(db,2*MiB,Category::Database,owner));
  assert(db[MiB-1]==42 && memory.snapshot().current==before.current);
  assert(!memory.release(db,other));
  assert(memory.snapshot().current==before.current);
  Json fail;
  assert(parseJson(R"({"after":1,"pool":"psram"})",fail).empty());
  failAllocation(fail);
  assert(!memory.reallocate(db,5*MiB/4+128,Category::Database,owner));
  assert(memory.snapshot().current==before.current && db[MiB-1]==42);
  db=static_cast<unsigned char*>(memory.reallocate(db,1024,Category::Database,owner));
  assert(db && db[1023]==42);
  assert(memory.snapshot().current<before.current);
  assert(memory.endOwner(owner)>0);
  assert(!memory.allocate(16,Category::Application,owner));
  assert(memory.release(db,owner));
  assert(memory.ownerBytes(owner)==0);
  auto successor=memory.beginOwner(1);assert(successor && successor!=owner);
  assert(!memory.allocate(16,Category::Application,owner));
  assert(memory.release(font,other));
  memory.endOwner(other);memory.endOwner(successor);
  assert(memory.snapshot().current==0);
  // Physical admission accounts for unclassified driver/library allocations.
  auto* raw=heap_caps_malloc(14*MiB,MALLOC_CAP_SPIRAM);assert(raw);
  assert(memory.snapshot().unclassified>=14*MiB);
  assert(!memory.allocate(1,Category::Application));
  assert(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)==65536);
  heap_caps_free(raw);
  const auto stats=memory.snapshot();
  assert(stats.failures[size_t(Failure::Heap)]==1);
  assert(stats.failures[size_t(Failure::GlobalReserve)]==1);
  assert(stats.failures[size_t(Failure::ForeignOwner)]==1);
  assert(stats.current==0 && stats.free==stats.capacity);
}
