#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
namespace freeink::font {
// Fixed metadata over caller-owned pixels. Reclaim only whole cold blocks;
// never overwrite glyphs belonging to the displayed or candidate page. When
// both pages fill the arena the caller rasterizes uncached, at full quality.
class PageGlyphArena {
 public:
  static constexpr size_t blockBytes=32*1024,maxBlocks=64;
  void reset(size_t bytes) {blocks_={};capacity_=std::min(bytes,blockBytes*maxBlocks);count_=(capacity_+blockBytes-1)/blockBytes;tail_=0;protect(0,0,0);}
  void protect(uint64_t current,uint64_t adjacent,uint64_t drawing) {current_=current;adjacent_=adjacent;drawing_=drawing;}
  bool valid(size_t offset,uint32_t generation) const {
    const size_t i=offset/blockBytes;
    return i<count_ && generation && blocks_[i].generation==generation && offset%blockBytes<blocks_[i].used;
  }
  bool pinned(size_t offset) const {return offset<capacity_ && pinnedBlock(blocks_[offset/blockBytes]);}
  void touch(size_t offset) {
    if(!drawing_ || offset>=capacity_)return;
    auto& b=blocks_[offset/blockBytes];
    if(b.pages[0]==drawing_ || b.pages[1]==drawing_)return;
    b.pages[protectedPage(b.pages[0])?1:0]=drawing_;
  }
  bool allocate(size_t bytes,size_t& offset,uint32_t& generation) {
    bytes=std::max(size_t(1),bytes);
    if(!count_ || bytes>blockBytes)return false;
    for(size_t visit=0;visit<2*count_;++visit) {
      const size_t i=(tail_+visit+(visit>=count_?1:0))%count_;
      auto& b=blocks_[i];
      const size_t limit=std::min(blockBytes,capacity_-i*blockBytes);
      if(bytes>limit)continue;
      if(!b.generation || bytes>limit-b.used) {
        if(b.generation && visit<count_)continue;
        if(pinnedBlock(b) || b.generation==UINT32_MAX)continue;
        const auto next=b.generation+1;b={};b.generation=next;
      }
      tail_=i;offset=i*blockBytes+b.used;b.used+=bytes;generation=b.generation;
      touch(offset);return true;
    }
    return false;
  }
 private:
  struct Block {uint64_t pages[2]{};uint32_t generation=0,used=0;};
  bool protectedPage(uint64_t page)const{return page && (page==current_ || page==adjacent_);}
  bool pinnedBlock(const Block& b)const{return protectedPage(b.pages[0]) || protectedPage(b.pages[1]);}
  std::array<Block,maxBlocks> blocks_{};
  size_t capacity_=0,count_=0,tail_=0;
  uint64_t current_=0,adjacent_=0,drawing_=0;
};
}
