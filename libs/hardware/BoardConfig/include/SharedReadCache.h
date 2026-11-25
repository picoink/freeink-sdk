#pragma once
#include <ResourceMemory.h>
#include <cstring>
#include <cstdio>
namespace freeink::resources {
// Immutable blocks are copied under the directory lock. Misses are filled into
// a private block outside it, then published only if the slot generation still
// matches. Reclaim, resize and file I/O can safely reenter the cache.
class SharedReadCache {
  // Sparse CJK glyph outlines are usually much smaller than a 4 KiB block.
  // Sector-aligned 1 KiB blocks reduce SD read amplification; four ways keep
  // hot cmap/loca/metrics blocks from colliding with streamed outlines.
  static constexpr size_t blockSize=1024;
  struct Tag {uint32_t block,used;};
  struct Slot {char key[160]{};uint64_t version=0,generation=0;size_t size=0,refs=0,count=0;uint8_t* bytes=nullptr;uint32_t clock=0;};
  struct Pool {std::mutex mutex;std::array<Slot,8> slots;};
  static Pool& pool(){alignas(Pool) static unsigned char storage[sizeof(Pool)];static auto* p=new(storage) Pool;return *p;}
 public:
  using Read=size_t(*)(void*,size_t,uint8_t*,size_t);
  ~SharedReadCache(){close();}
  SharedReadCache()=default;
  SharedReadCache(const SharedReadCache&)=delete;
  SharedReadCache& operator=(const SharedReadCache&)=delete;
  void open(const char* key,size_t size,uint64_t version) {
    close();if(!key || strlen(key)>=sizeof(Slot::key))return;
    auto& p=pool();std::lock_guard<std::mutex> lock(p.mutex);
    for(auto& s:p.slots)if(s.refs && s.size==size && s.version==version && !strcmp(s.key,key)){slot_=&s;++s.refs;return;}
    for(auto& s:p.slots)if(!s.refs){snprintf(s.key,sizeof(s.key),"%s",key);s.size=size;s.version=version;s.refs=1;++s.generation;slot_=&s;return;}
  }
  void configure(size_t requested) {
    requested_=requested;
    auto& p=pool();Slot* slot;size_t count;uint64_t generation;
    {std::lock_guard<std::mutex> lock(p.mutex);slot=slot_;if(!slot)return;
      count=std::min(requested/blockSize,(slot->size+blockSize-1)/blockSize);
      if(count<=slot->count)return;generation=slot->generation;}
    const auto snapshot=Memory::get().snapshot();
    const auto used=snapshot.categories[size_t(Category::Font)].current;
    // Reclaimed file blocks must become a cache again, even when a newly
    // opened face leaves less room than the original request. Leave space for
    // FreeType's live glyph work, and never consume the physical reserve.
    constexpr size_t workspace=64*1024;
    const auto categoryRoom=limits[size_t(Category::Font)]-used;
    const auto physicalRoom=snapshot.free>reserveBytes?snapshot.free-reserveBytes:0;
    const auto globalLimit=snapshot.capacity>reserveBytes?snapshot.capacity-reserveBytes:0;
    const auto globalRoom=globalLimit>snapshot.current?globalLimit-snapshot.current:0;
    const auto available=std::min({categoryRoom>workspace?categoryRoom-workspace:0,physicalRoom,globalRoom});
    while(count && count*(blockSize+sizeof(Tag))+128>available)count/=2;
    if(!count)return;
    {std::lock_guard<std::mutex> lock(p.mutex);if(slot_!=slot || slot->generation!=generation || count<=slot->count)return;}
    const auto bytes=count*(blockSize+sizeof(Tag));
    auto* replacement=static_cast<uint8_t*>(Memory::get().allocate(bytes,Category::Font,0,Lifetime::Cache));
    if(!replacement)return;
    memset(replacement+count*blockSize,255,count*sizeof(Tag));
    uint8_t* retired=replacement;
    {std::lock_guard<std::mutex> lock(p.mutex);
      if(slot_==slot && slot->generation==generation){retired=slot->bytes;slot->bytes=replacement;slot->count=count;++slot->generation;}}
    Memory::get().release(retired);
  }
  void trim() {
    auto& p=pool();uint8_t* retired=nullptr;
    {std::lock_guard<std::mutex> lock(p.mutex);if(slot_){retired=slot_->bytes;slot_->bytes=nullptr;slot_->count=0;++slot_->generation;}}
    Memory::get().release(retired);
  }
  void close() {
    auto& p=pool();uint8_t* retired=nullptr;
    {std::lock_guard<std::mutex> lock(p.mutex);if(slot_ && !--slot_->refs){retired=slot_->bytes;slot_->bytes=nullptr;slot_->count=0;++slot_->generation;}slot_=nullptr;requested_=0;}
    Memory::get().release(retired);
  }
  size_t read(size_t offset,uint8_t* out,size_t bytes,Read fn,void* context,uint32_t& hits) {
    auto& p=pool();size_t copied=0;std::array<uint8_t,blockSize> blockBytes;
    bool restore=false;
    {std::lock_guard<std::mutex> lock(p.mutex);restore=slot_ && !slot_->count && requested_ && bytes;}
    // At most one restoration attempt per read, outside the directory lock.
    // A concurrent/reentrant trim during the fill still invalidates publication.
    if(restore)configure(requested_);
    while(copied<bytes) {
      Slot* slot;uint64_t generation;size_t size,count,n;
      const size_t block=offset/blockSize,within=offset%blockSize;
      {std::lock_guard<std::mutex> lock(p.mutex);
        slot=slot_;size=slot?slot->size:0;count=slot?slot->count:0;generation=slot?slot->generation:0;
        if(slot && offset>=size)break;
        n=std::min({bytes-copied,blockSize-within,slot?size-offset:bytes-copied});
        if(count){const auto index=find(*slot,block);auto& tag=tags(*slot)[index];
          if(tag.block==block){tag.used=++slot->clock;memcpy(out+copied,slot->bytes+index*blockSize+within,n);offset+=n;copied+=n;++hits;continue;}}}
      if(!count)return copied+fn(context,offset,out+copied,bytes-copied);
      // FreeType retains large tables itself. Let their cold reads use the
      // storage driver's multi-sector transfer instead of many tiny requests.
      if(bytes-copied>=4*blockSize)
        return copied+fn(context,offset,out+copied,std::min(bytes-copied,size-offset));
      const size_t wanted=std::min(blockSize,size-block*blockSize);
      if(fn(context,block*blockSize,blockBytes.data(),wanted)!=wanted)break;
      memcpy(out+copied,blockBytes.data()+within,n);
      {std::lock_guard<std::mutex> lock(p.mutex);
        if(slot_==slot && slot->generation==generation && slot->count==count){
          const auto index=find(*slot,block);
          memcpy(slot->bytes+index*blockSize,blockBytes.data(),wanted);tags(*slot)[index]={uint32_t(block),++slot->clock};}}
      offset+=n;copied+=n;
    }
    return copied;
  }
 private:
  static Tag* tags(Slot& s){return reinterpret_cast<Tag*>(s.bytes+s.count*blockSize);}
  static size_t find(Slot& s,size_t block) {
    const size_t ways=std::min(size_t(4),s.count),first=(block%(s.count/ways))*ways;
    size_t oldest=first;
    for(size_t i=first;i<first+ways;++i){
      const auto& tag=tags(s)[i];const auto& candidate=tags(s)[oldest];
      if(tag.block==block)return i;
      if(tag.block==UINT32_MAX || (candidate.block!=UINT32_MAX && uint32_t(s.clock-tag.used)>uint32_t(s.clock-candidate.used)))oldest=i;
    }
    return oldest;
  }
  Slot* slot_=nullptr;
  size_t requested_=0;
};
}
