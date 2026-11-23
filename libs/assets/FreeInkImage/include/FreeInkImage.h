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
  static uint32_t capabilities(); // JPEG=1, RGB-to-gray PPA=2, progressive=4
  ImageAccelerator()=default;
  ~ImageAccelerator();
  void configure(const Work& work) { work_=work; }
  const Stats& stats() const { return stats_; }
  void resetStats() { stats_={}; }
  Result render(book::BookSource&,const book::ZipEntry&,const book::PageImage&,
                book::Arena&,book::ImageRowFn,void*) override;
 private:
  Work work_;
  Stats stats_;
  void* jpeg_=nullptr;
  void* ppa_=nullptr;
  bool proceed() const { return !work_.proceed || work_.proceed(work_.context); }
  Result declined(Reason reason) { ++stats_.fallback; stats_.reason=reason; return Result::Declined; }
  Result decode(book::BookSource&,const book::ZipEntry&,const book::PageImage&,
                book::Arena&,book::ImageRowFn,void*);
};
}
