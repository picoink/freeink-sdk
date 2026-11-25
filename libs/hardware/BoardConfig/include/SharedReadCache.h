#pragma once
#include <ResourceMemory.h>
#include <cstring>
#include <cstdio>
namespace freeink::resources {
// Immutable blocks are copied under the directory lock. Misses are filled into
// a private block outside it, then published only if the slot generation still
// matches. Reclaim, resize and file I/O can safely reenter the cache.
class SharedReadCache {
  static constexpr size_t blockSize=4096;
  struct Slot {char key[160]{};uint64_t version=0,generation=0;size_t size=0,refs=0,count=0;uint8_t* bytes=nullptr;};
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
    auto& p=pool();Slot* slot;size_t count;uint64_t generation;
    {std::lock_guard<std::mutex> lock(p.mutex);slot=slot_;if(!slot)return;
      count=std::min(requested/blockSize,(slot->size+blockSize-1)/blockSize);
      if(count<=slot->count)return;generation=slot->generation;}
    const auto usage=Memory::get().snapshot().categories[size_t(Category::Font)];
    const auto bytes=count*(blockSize+sizeof(uint32_t));
    if(bytes+128>limits[size_t(Category::Font)]-usage.current)return;
    auto* replacement=static_cast<uint8_t*>(Memory::get().allocate(bytes,Category::Font,0,Lifetime::Cache));
    if(!replacement)return;
    memset(replacement+count*blockSize,255,count*sizeof(uint32_t));
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
    {std::lock_guard<std::mutex> lock(p.mutex);if(slot_ && !--slot_->refs){retired=slot_->bytes;slot_->bytes=nullptr;slot_->count=0;++slot_->generation;}slot_=nullptr;}
    Memory::get().release(retired);
  }
  size_t read(size_t offset,uint8_t* out,size_t bytes,Read fn,void* context,uint32_t& hits) {
    auto& p=pool();size_t copied=0;std::array<uint8_t,blockSize> blockBytes;
    while(copied<bytes) {
      Slot* slot;uint64_t generation;size_t size,count,n;
      const size_t block=offset/blockSize,within=offset%blockSize;
      {std::lock_guard<std::mutex> lock(p.mutex);
        slot=slot_;size=slot?slot->size:0;count=slot?slot->count:0;generation=slot?slot->generation:0;
        if(slot && offset>=size)break;
        n=std::min({bytes-copied,blockSize-within,slot?size-offset:bytes-copied});
        if(count && tags(*slot)[block%count]==block){memcpy(out+copied,slot->bytes+(block%count)*blockSize+within,n);offset+=n;copied+=n;++hits;continue;}}
      if(!count)return copied+fn(context,offset,out+copied,bytes-copied);
      const size_t wanted=std::min(blockSize,size-block*blockSize);
      if(fn(context,block*blockSize,blockBytes.data(),wanted)!=wanted)break;
      memcpy(out+copied,blockBytes.data()+within,n);
      {std::lock_guard<std::mutex> lock(p.mutex);
        if(slot_==slot && slot->generation==generation && slot->count==count){
          memcpy(slot->bytes+(block%count)*blockSize,blockBytes.data(),wanted);tags(*slot)[block%count]=block;}}
      offset+=n;copied+=n;
    }
    return copied;
  }
 private:
  static uint32_t* tags(Slot& s){return reinterpret_cast<uint32_t*>(s.bytes+s.count*blockSize);}
  Slot* slot_=nullptr;
};
}
