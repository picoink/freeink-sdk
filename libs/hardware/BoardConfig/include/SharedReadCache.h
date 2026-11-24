#pragma once
#include <ResourceMemory.h>
#include <cstring>
#include <cstdio>
namespace freeink::resources {
// Small fixed directory of immutable file blocks. Each reader retains its own
// file handle; only validated bytes are shared. A changed version gets a new
// slot, and the last reference releases the charged storage.
class SharedReadCache {
  static constexpr size_t blockSize=4096;
  struct Slot {char key[160]{};uint64_t version=0;size_t size=0,refs=0,count=0;uint8_t* bytes=nullptr;};
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
    for(auto& s:p.slots)if(!s.refs){snprintf(s.key,sizeof(s.key),"%s",key);s.size=size;s.version=version;s.refs=1;slot_=&s;return;}
  }
  void configure(size_t requested) {
    auto& p=pool();std::lock_guard<std::mutex> lock(p.mutex);if(!slot_)return;auto& s=*slot_;
    const auto count=std::min(requested/blockSize,(s.size+blockSize-1)/blockSize);if(count<=s.count)return;
    // Cache sizing is best effort. Do not trigger global eviction just because
    // a preferred size does not fit; active glyphs have first claim on space.
    const auto usage=Memory::get().snapshot().categories[size_t(Category::Font)];
    if(count*(blockSize+sizeof(uint32_t))+128>limits[size_t(Category::Font)]-usage.current)return;
    auto* bytes=static_cast<uint8_t*>(Memory::get().reallocate(s.bytes,count*(blockSize+sizeof(uint32_t)),Category::Font,0,Lifetime::Cache));
    if(!bytes)return;s.bytes=bytes;s.count=count;memset(tags(s),255,count*sizeof(uint32_t));
  }
  void trim() {auto& p=pool();std::lock_guard<std::mutex> lock(p.mutex);if(slot_){Memory::get().release(slot_->bytes);slot_->bytes=nullptr;slot_->count=0;}}
  void close() {auto& p=pool();std::lock_guard<std::mutex> lock(p.mutex);if(slot_ && !--slot_->refs){Memory::get().release(slot_->bytes);*slot_={};}slot_=nullptr;}
  size_t read(size_t offset,uint8_t* out,size_t bytes,Read fn,void* context,uint32_t& hits) {
    auto& p=pool();std::lock_guard<std::mutex> lock(p.mutex);
    if(!slot_ || !slot_->count)return fn(context,offset,out,bytes);
    auto& s=*slot_;size_t copied=0;
    while(copied<bytes && offset<s.size) {
      const size_t block=offset/blockSize,index=block%s.count,within=offset%blockSize;
      auto* data=s.bytes+index*blockSize;
      if(tags(s)[index]!=block) {
        tags(s)[index]=UINT32_MAX;const auto wanted=std::min(blockSize,s.size-block*blockSize);
        if(fn(context,block*blockSize,data,wanted)!=wanted)break;tags(s)[index]=block;
      } else ++hits;
      const auto n=std::min({bytes-copied,blockSize-within,s.size-offset});memcpy(out+copied,data+within,n);offset+=n;copied+=n;
    }
    return copied;
  }
 private:
  static uint32_t* tags(Slot& s){return reinterpret_cast<uint32_t*>(s.bytes+s.count*blockSize);}
  Slot* slot_=nullptr;
};
}
