#include "FreeInkImage.h"
#include <ProgressiveJpeg.h>
#include <render/ImageRowScaler.h>
#include <epub/ImageProbe.h>
#include <ApplicationMemory.h>
#include <algorithm>
#include <memory>
#if defined(CONFIG_IDF_TARGET_ESP32S31) && !defined(FREEINK_HOST)
#define FREEINK_S31_IMAGE 1
#include <driver/jpeg_decode.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#endif
namespace freeink {
namespace {
using Buffer=std::unique_ptr<uint8_t,decltype(&free)>;
constexpr auto caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
constexpr size_t reserve=2*1024*1024;
bool room(size_t bytes,size_t largest,size_t budget) {
  const auto available=heap_caps_get_free_size(caps);
  return bytes<=budget && available>=reserve && bytes<=available-reserve &&
    largest<=heap_caps_get_largest_free_block(caps);
}
struct ProgressiveContext {
  book::ZipEntryReader reader;
  book::RowScaler scaler;
  ImageAccelerator::Work work;
  bool proceed() { return !work.proceed || work.proceed(work.context); }
  static int32_t read(void* opaque,uint8_t* out,uint32_t count) {
    auto& c=*static_cast<ProgressiveContext*>(opaque);
    return c.proceed() ? c.reader.read(out,count) : -1;
  }
  static bool row(void* opaque,uint16_t y,const uint8_t* data,uint16_t) {
    auto& c=*static_cast<ProgressiveContext*>(opaque);
    return c.proceed() && c.scaler.feedSourceRow(y,data);
  }
};
#if FREEINK_S31_IMAGE
// Standard baseline sampling only; padding must agree with the DMA driver.
bool standard(const uint8_t* data,size_t size) {
  if (size<4 || data[0]!=0xff || data[1]!=0xd8) return false;
  for (size_t at=2;at+4<=size;) {
    if (data[at++]!=0xff) return false;
    while (at<size && data[at]==0xff) ++at;
    if (at>=size) return false;
    unsigned marker=data[at++];
    if (marker==0xda || marker==0xd9 || at+2>size) return false;
    const size_t n=(size_t(data[at])<<8)|data[at+1];
    if (n<2 || n>size-at) return false;
    if (marker==0xc0) {
      if (n<11 || data[at+2]!=8) return false;
      const unsigned count=data[at+7],sampling=data[at+9];
      if (count==1) return sampling==0x11;
      return count==3 && n>=17 && (sampling==0x11 || sampling==0x21 || sampling==0x22) && data[at+12]==0x11 && data[at+15]==0x11;
    }
    at+=n;
  }
  return false;
}
struct HardwareLease {
  ImageAccelerator::Work work; bool held=false;
  explicit HardwareLease(const ImageAccelerator::Work& w):work(w) {
    held=!w.enterHardware || w.enterHardware(w.context);
  }
  ~HardwareLease() { if (held && work.leaveHardware) work.leaveHardware(work.context); }
};
#endif
}
uint32_t ImageAccelerator::capabilities() {
#if FREEINK_S31_IMAGE
  return 7;
#else
  return 4;
#endif
}
ImageAccelerator::~ImageAccelerator() {
#if FREEINK_S31_IMAGE
  if (ppa_) ppa_unregister_client(static_cast<ppa_client_handle_t>(ppa_));
  if (jpeg_) jpeg_del_decoder_engine(static_cast<jpeg_decoder_handle_t>(jpeg_));
#endif
}
book::ImageBackend::Result ImageAccelerator::render(book::BookSource& source,const book::ZipEntry& entry,
    const book::PageImage& image,book::Arena& scratch,book::ImageRowFn rowFn,void* user) {
  const auto result=decode(source,entry,image,scratch,rowFn,user);
  if (!proceed()) { stats_.reason=Reason::Cancelled; return Result::Stopped; }
  return result;
}
book::ImageBackend::Result ImageAccelerator::decode(book::BookSource& source,const book::ZipEntry& entry,
    const book::PageImage& image,book::Arena& scratch,book::ImageRowFn rowFn,void* user) {
  ++stats_.attempts;
  if (!proceed()) { stats_.reason=Reason::Cancelled; return Result::Stopped; }
  if (work_.software) return declined(Reason::SoftwareRequested);
  book::ImageInfo info;
  if (book::probeImage(source,entry,scratch,&info)!=book::BookStatus::Ok || info.kind!=book::ImageInfo::Kind::Jpeg)
    return declined(Reason::Format);
  if (!info.width || !info.height || info.width>8192 || info.height>8192 ||
      image.width>info.width || image.height>info.height) return declined(Reason::Dimensions);
  if (info.progressive) {
    const auto bytes=ProgressiveJpeg::storeBytes(info.width,info.height);
    if (!room(bytes,bytes,work_.budget)) return declined(Reason::Budget);
    ProgressiveContext c; c.work=work_;
    c.scaler.srcW=info.width; c.scaler.srcH=info.height;
    c.scaler.dstW=image.width; c.scaler.dstH=image.height; c.scaler.rowFn=rowFn; c.scaler.user=user;
    if (!book::initScaler(c.scaler,scratch)) return declined(Reason::Allocation);
    const auto mark=scratch.mark();
    if (c.reader.open(source,entry,scratch)!=book::BookStatus::Ok) return declined(Reason::Format);
    ProgressiveJpeg decoder;
    auto started=micros();
    if (decoder.begin(ProgressiveContext::read,&c)!=ProgressiveJpeg::Status::Ok) return declined(Reason::Format);
    scratch.release(mark);
    if (c.reader.open(source,entry,scratch)!=book::BookStatus::Ok) return declined(Reason::Format);
    const auto result=decoder.decode(ProgressiveContext::read,&c,ProgressiveContext::row,&c);
    stats_.decode_us+=uint32_t(micros()-started); stats_.peak_bytes=std::max<uint32_t>(stats_.peak_bytes,bytes);
    if (!proceed() || result==ProgressiveJpeg::Status::Stopped || c.scaler.stopped) return Result::Stopped;
    if (result!=ProgressiveJpeg::Status::Ok) return declined(Reason::Format);
    ++stats_.progressive; return Result::Complete;
  }
#if !FREEINK_S31_IMAGE
  return declined(Reason::Platform);
#else
  if ((uint32_t(info.width)*info.height)%8 || entry.uncompressedSize>2*1024*1024) return declined(Reason::Dimensions);
  if (!room(entry.uncompressedSize,entry.uncompressedSize,work_.budget)) return declined(Reason::Budget);
  size_t inputCapacity=0;
  jpeg_decode_memory_alloc_cfg_t memory{}; memory.buffer_direction=JPEG_DEC_ALLOC_INPUT_BUFFER;
  Buffer input(static_cast<uint8_t*>(jpeg_alloc_decoder_mem(entry.uncompressedSize,&memory,&inputCapacity)),&free);
  if (!input || inputCapacity<entry.uncompressedSize) return declined(Reason::Allocation);
  book::ZipEntryReader reader;
  if (reader.open(source,entry,scratch)!=book::BookStatus::Ok) return declined(Reason::Format);
  for (uint32_t at=0;at<entry.uncompressedSize;) {
    if (!proceed()) return Result::Stopped;
    const auto n=reader.read(input.get()+at,std::min<uint32_t>(4096,entry.uncompressedSize-at));
    if (n<=0) return declined(Reason::Format); at+=n;
  }
  jpeg_decode_picture_info_t header{};
  if (!standard(input.get(),entry.uncompressedSize) || jpeg_decoder_get_info(input.get(),entry.uncompressedSize,&header)!=ESP_OK)
    return declined(Reason::Format);
  const bool gray=header.sample_method==JPEG_DOWN_SAMPLING_GRAY;
  const unsigned mx=header.sample_method==JPEG_DOWN_SAMPLING_YUV420 || header.sample_method==JPEG_DOWN_SAMPLING_YUV422 ? 16 : 8;
  const unsigned my=header.sample_method==JPEG_DOWN_SAMPLING_YUV420 ? 16 : 8;
  const unsigned stride=(info.width+mx-1)/mx*mx,height=(info.height+my-1)/my*my;
  const size_t rawBytes=size_t(stride)*height*(gray ? 1 : 3);
  size_t grayBytes=!gray && work_.rgb && work_.ppa ? size_t(stride)*height : 0;
  if (inputCapacity>work_.budget) return declined(Reason::Budget);
  if (!room(rawBytes+grayBytes+128,rawBytes,work_.budget-inputCapacity)) {
    grayBytes=0;
    if (inputCapacity>work_.budget || !room(rawBytes,rawBytes,work_.budget-inputCapacity)) return declined(Reason::Budget);
  }
  memory.buffer_direction=JPEG_DEC_ALLOC_OUTPUT_BUFFER; size_t rawCapacity=0;
  Buffer raw(static_cast<uint8_t*>(jpeg_alloc_decoder_mem(rawBytes,&memory,&rawCapacity)),&free);
  if (!raw || rawCapacity<rawBytes) return declined(Reason::Allocation);
  const size_t alignment=std::max<size_t>(64,esp_cache_get_line_size_by_addr(raw.get()));
  grayBytes=(grayBytes+alignment-1)/alignment*alignment;
  Buffer converted(grayBytes ? static_cast<uint8_t*>(heap_caps_aligned_alloc(alignment,grayBytes,caps)) : nullptr,&free);
  stats_.peak_bytes=std::max<uint32_t>(stats_.peak_bytes,inputCapacity+rawCapacity+(converted ? grayBytes : 0));
  bool convertedOk=false;
  {
    HardwareLease lease(work_);
    if (!lease.held) return Result::Stopped;
    if (!jpeg_) {
      jpeg_decode_engine_cfg_t config{}; config.timeout_ms=1000;
      jpeg_decoder_handle_t decoder=nullptr;
      if (jpeg_new_decoder_engine(&config,&decoder)!=ESP_OK) return declined(Reason::Driver);
      jpeg_=decoder;
    }
    jpeg_decode_cfg_t config{}; config.output_format=gray ? JPEG_DECODE_OUT_FORMAT_GRAY : work_.rgb ? JPEG_DECODE_OUT_FORMAT_RGB888 : JPEG_DECODE_OUT_FORMAT_YUV444;
    config.rgb_order=JPEG_DEC_RGB_ELEMENT_ORDER_BGR; config.conv_std=JPEG_YUV_RGB_CONV_STD_BT601;
    uint32_t written=0; auto started=micros();
    esp_cache_msync(input.get(),inputCapacity,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    const auto result=jpeg_decoder_process(static_cast<jpeg_decoder_handle_t>(jpeg_),&config,input.get(),entry.uncompressedSize,raw.get(),rawCapacity,&written);
    stats_.decode_us+=uint32_t(micros()-started);
    if (result!=ESP_OK || written<rawBytes) return declined(Reason::Driver);
    esp_cache_msync(raw.get(),rawCapacity,ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    ++stats_.hardware;
    // PPA only converts at unit scale. Area reduction preserves small strokes.
    if (converted) {
      if (!ppa_) {
        ppa_client_config_t config{}; config.oper_type=PPA_OPERATION_SRM; config.max_pending_trans_num=1;
        ppa_client_handle_t client=nullptr;
        if (ppa_register_client(&config,&client)==ESP_OK) { ppa_=client; ppa_set_rgb2gray_formula(77,150,29); }
      }
      if (ppa_) {
        ppa_srm_oper_config_t config{};
        config.in.buffer=raw.get(); config.in.pic_w=stride; config.in.pic_h=height;
        config.in.block_w=info.width; config.in.block_h=info.height; config.in.srm_cm=PPA_SRM_COLOR_MODE_RGB888;
        config.out.buffer=converted.get(); config.out.buffer_size=grayBytes;
        config.out.pic_w=stride; config.out.pic_h=height; config.out.srm_cm=PPA_SRM_COLOR_MODE_GRAY8;
        config.scale_x=config.scale_y=1; config.mode=PPA_TRANS_MODE_BLOCKING;
        started=micros(); convertedOk=ppa_do_scale_rotate_mirror(static_cast<ppa_client_handle_t>(ppa_),&config)==ESP_OK;
        stats_.convert_us+=uint32_t(micros()-started);
        if (convertedOk) { esp_cache_msync(converted.get(),grayBytes,ESP_CACHE_MSYNC_FLAG_DIR_M2C); ++stats_.ppa; }
      }
    }
  }
  input.reset();
  if (!proceed()) return Result::Stopped;
  book::RowScaler scaler;
  scaler.srcW=info.width; scaler.srcH=info.height; scaler.dstW=image.width; scaler.dstH=image.height;
  scaler.rowFn=rowFn; scaler.user=user;
  auto* row=static_cast<uint8_t*>(scratch.alloc(info.width,1));
  if (!book::initScaler(scaler,scratch) || !row) return declined(Reason::Allocation);
  auto started=micros();
  for (unsigned y=0;y<info.height;++y) {
    if (!proceed()) return Result::Stopped;
    const uint8_t* pixels=convertedOk ? converted.get()+size_t(y)*stride : raw.get()+size_t(y)*stride*(gray ? 1 : 3);
    if (!gray && !convertedOk) {
      // The installed S31 decoder's unswizzled YUV444 DMA output is V,U,Y.
      // Extract full-range JPEG luma; RGB CSC uses limited-range BT.601.
      for (unsigned x=0;x<info.width;++x) row[x]=work_.rgb ? (29*pixels[3*x]+150*pixels[3*x+1]+77*pixels[3*x+2])>>8 : pixels[3*x+2];
      pixels=row;
    }
    if (!scaler.feedSourceRow(y,pixels)) return Result::Stopped;
  }
  stats_.scale_us+=uint32_t(micros()-started);
  return Result::Complete;
#endif
}
}
