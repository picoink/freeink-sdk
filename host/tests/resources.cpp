#include <ResourceMemory.h>
#include <SharedReadCache.h>
#include <InternalMemory.h>
#include <TlsMemory.h>
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
  assert(!InternalMemory::get().admitRadio() && InternalMemory::get().radioDenied()==1);
  const auto owner=memory.beginOwner(1),other=memory.beginOwner(2);
  assert(owner && other && owner!=other);
  auto* db=static_cast<unsigned char*>(memory.allocate(MiB,Category::Database,owner,Lifetime::Session));
  assert(db && !(uintptr_t(db)%alignof(std::max_align_t)));
  memset(db,42,MiB);
  // A font cannot consume the independent database quota.
  auto* font=memory.allocate(3*MiB-256,Category::Font,other,Lifetime::Cache);assert(font);
  assert(!memory.allocate(512,Category::Font,other));
  assert(memory.takePressure()==pressureFor(Category::Font));
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
  memory.takePressure();
  // Physical admission accounts for unclassified driver/library allocations.
  auto* raw=heap_caps_malloc(14*MiB,MALLOC_CAP_SPIRAM);assert(raw);
  assert(memory.snapshot().unclassified>=14*MiB);
  assert(!memory.allocate(1,Category::Application));
  assert(memory.takePressure()==globalPressure);
  assert(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)==65536);
  heap_caps_free(raw);
  auto* moving=static_cast<unsigned char*>(memory.allocate(MiB,Category::Database));assert(moving);moving[0]=42;
  auto* pressure=heap_caps_malloc(25*MiB/2,MALLOC_CAP_SPIRAM);assert(pressure);
  assert(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>reserveBytes+MiB/4);
  // Delta fits the category and free space, but a moved block would cross the
  // physical reserve before its old allocation can be released.
  assert(!memory.reallocate(moving,5*MiB/4,Category::Database,0) && moving[0]==42);
  heap_caps_free(pressure);memory.release(moving);
  void* fragments[15];
  for(auto& p:fragments){p=heap_caps_malloc(MiB,MALLOC_CAP_SPIRAM);assert(p);}
  for(unsigned i=0;i<15;i+=2){heap_caps_free(fragments[i]);fragments[i]=nullptr;}
  assert(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>5*MiB/2+reserveBytes);
  memory.takePressure();
  assert(!memory.allocate(5*MiB/2,Category::System));
  assert(memory.takePressure()==globalPressure);
  assert(memory.snapshot().current==0);
  for(auto p:fragments)heap_caps_free(p);
  auto abandoned=memory.beginOwner(7);
  auto* executable=allocateAligned(853,64,Category::Application,abandoned);
  assert(executable && !(uintptr_t(executable)&63));memset(executable,42,853);releaseAligned(executable);
  assert(memory.ownerBytes(abandoned)==0);
  auto first=memory.allocate(1024,Category::Application,abandoned);
  assert(first && memory.allocate(1024,Category::Application,abandoned));
  first=memory.reallocate(first,8192,Category::Application,abandoned);assert(first);
  assert(memory.allocate(128,Category::Transient,abandoned));
  assert(memory.reclaimOwner(abandoned)>0 && memory.ownerBytes(abandoned)==0);
  {
    SharedReadCache first,second,changed;
    first.open("font.ttf",8192,1);first.configure(8192);
    const auto charged=memory.snapshot().current;assert(charged>8192);
    second.open("font.ttf",8192,1);second.configure(8192);assert(memory.snapshot().current==charged);
    unsigned reads=0;uint32_t hits=0;uint8_t output[16]{};
    auto read=[](void* p,size_t at,uint8_t* out,size_t bytes)->size_t {++*static_cast<unsigned*>(p);memset(out,int(at/4096+42),bytes);return bytes;};
    assert(first.read(0,output,16,read,&reads,hits)==16 && output[0]==42 && reads==1);
    first.close();assert(second.read(0,output,16,read,&reads,hits)==16 && reads==1);
    changed.open("font.ttf",8192,2);changed.configure(8192);
    assert(changed.read(0,output,16,read,&reads,hits)==16 && reads==2);
    second.trim();assert(second.read(0,output,16,read,&reads,hits)==16 && reads==3);
  }
  auto* tls=static_cast<unsigned char*>(TlsMemory::calloc(128,1024));assert(tls && tls[0]==0 && tls[128*1024-1]==0);
  assert(!TlsMemory::calloc(SIZE_MAX,2));
  assert(!TlsMemory::calloc(512,1024));TlsMemory::free(tls);
  bool called=false;
  assert(!InternalMemory::get().createTask(64*1024,[&]{called=true;return true;}) && !called);
  assert(InternalMemory::get().createTask(16*1024,[&]{called=true;return true;}) && called);
  const auto stats=memory.snapshot();
  assert(stats.failures[size_t(Failure::Heap)]==2);
  assert(stats.failures[size_t(Failure::GlobalReserve)]==2);
  assert(stats.failures[size_t(Failure::ForeignOwner)]==1);
  assert(stats.current==0 && stats.free==stats.capacity);
}
