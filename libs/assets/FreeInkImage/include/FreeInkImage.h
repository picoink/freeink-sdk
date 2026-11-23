#pragma once
#include <render/ImageRenderer.h>
#include <cstddef>
#include <cstdint>
namespace freeink {
// No ESP-IDF types cross this boundary. Create, use and destroy on one worker.
class ImageAccelerator final : public book::ImageBackend {
 public:
  enum class Reason : uint32_t { None, SoftwareRequested, Format, Dimensions, Budget, Allocation, Driver, Cancelled, Platform };
  struct Work {
    bool (*proceed)(void*)=nullptr;
    bool (*enterHardware)(void*)=nullptr;
    void (*leaveHardware)(void*)=nullptr;
    void* context=nullptr;
    size_t budget=8*1024*1024;
    bool software=false, ppa=true, rgb=false;
  };
  struct Stats {
    uint32_t attempts=0, hardware=0, progressive=0, ppa=0, fallback=0;
    uint32_t decode_us=0, convert_us=0, scale_us=0, peak_bytes=0;
    Reason reason=Reason::None;
  };
  static uint32_t capabilities(); // JPEG decode=1, RGB-to-gray PPA=2, progressive=4, JPEG encode=8
  ImageAccelerator()=default;
  ~ImageAccelerator();
  // Reserve small internal DMA/control objects on the worker before UI heaps
  // fragment. The shared peripheral still executes one transaction at a time.
  void prepare();
  void configure(const Work& work) { work_=work; }
  const Stats& stats() const { return stats_; }
  void resetStats() { stats_={}; }
  Result render(book::BookSource&,const book::ZipEntry&,const book::PageImage&,
                book::Arena&,book::ImageRowFn,void*) override;
  // Baseline grayscale JPEG for persistent thumbnails. Dimensions must be
  // multiples of 16, at most 1024. No caller buffer is retained on return.
  bool encodeGray(const uint8_t*,uint16_t width,uint16_t height,unsigned quality,
                  uint8_t* output,size_t capacity,size_t& written);
 private:
  Work work_;
  Stats stats_;
  void* jpeg_=nullptr;
  void* encoder_=nullptr;
  void* ppa_=nullptr;
  bool proceed() const { return !work_.proceed || work_.proceed(work_.context); }
  Result declined(Reason reason) { ++stats_.fallback; stats_.reason=reason; return Result::Declined; }
  Result decode(book::BookSource&,const book::ZipEntry&,const book::PageImage&,
                book::Arena&,book::ImageRowFn,void*);
};
}
