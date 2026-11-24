#pragma once
#include <VolumeStorage.h>
#include <ResourceMemory.h>
#include <ApplicationMemory.h>
#include <algorithm>
#include <cstring>
namespace freeink {
// Optional, bounded random-read window. A miss reuses its open StorageFile;
// allocation failure preserves correctness by reading directly from that handle.
class StorageReadWindow {
 public:
  ~StorageReadWindow(){close();}
  bool open(const char* path) {close();file_=VolumeStorage::instance().open(path);return bool(file_);}
  void close() {file_.close();valid_=0;buffer_.reset();}
  explicit operator bool() const {return bool(file_);}
  uint64_t size() const {return file_.fileSize();}
  int readAt(uint64_t offset,void* out,size_t count) {
    if(!file_ || !out)return -1;
    if(count>kSize) return file_.seekSet(offset)?file_.read(out,count):-1;
    if(buffer_ && offset>=base_ && offset-base_<=valid_ && count<=valid_-(offset-base_)) {storageCacheHit(file_.volume());memcpy(out,buffer_.get()+offset-base_,count);return count;}
    if(!buffer_) buffer_.reset(static_cast<uint8_t*>(resources::Memory::get().allocate(kSize,resources::Category::Document)));
    if(!file_.seekSet(offset))return -1;
    if(!buffer_)return file_.read(out,count);
    const int got=file_.read(buffer_.get(),kSize);if(got<0){valid_=0;return -1;}
    base_=offset;valid_=got;const auto n=std::min<size_t>(count,valid_);memcpy(out,buffer_.get(),n);return n;
  }
 private:
  static constexpr size_t kSize=4096;
  StorageFile file_;
  resources::Buffer<uint8_t> buffer_{nullptr,&resources::release};
  uint64_t base_=0;size_t valid_=0;
};
}
