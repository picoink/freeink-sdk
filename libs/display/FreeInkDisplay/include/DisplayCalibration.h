#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace freeink {
// Four-direction adjustment semantics adapted from CloudZao/Metalio-E-INK4-Plus
// display_orient.cc (MIT; attribution in NOTICE). Values are portrait pixels.
struct DisplayCalibration {
  static constexpr int Limit = 20;
  int16_t side[4]{}; // top, bottom, left, right
  bool valid() const { for (auto v:side) if(v < -Limit || v > Limit)return false; return true; }
  int x() const { return side[3]-side[2]; }
  int y() const { return side[1]-side[0]; }
  int nativeX() const { return y(); }
  int nativeY() const { return -x(); }
  bool operator==(const DisplayCalibration& b) const { return std::equal(side,side+4,b.side); }
  // Inverse mapping before any UI orientation transform. Do not clamp touches
  // in an exposed white strip onto an unrelated edge control.
  bool unmap(float& x,float& y,int width,int height) const {
    x-=float(nativeX())/width;y-=float(nativeY())/height;
    return x>=0 && x<=1 && y>=0 && y<=1;
  }
};

// Translate the existing low-nibble-first Gray4 target in place. The application
// source and the previous physical frame stay unchanged; no scratch frame/row.
inline void translateGray4(uint8_t* pixels,int width,int height,int dx,int dy) {
  if(!dx && !dy)return;
  const size_t stride=width/2;
  if(dx<=-width || dx>=width || dy<=-height || dy>=height){memset(pixels,255,stride*height);return;}
  if(dy>0) { memmove(pixels+dy*stride,pixels,(height-dy)*stride);memset(pixels,255,dy*stride); }
  else if(dy<0) { const size_t rows=size_t(-dy);memmove(pixels,pixels+rows*stride,(height-rows)*stride);memset(pixels+(height-rows)*stride,255,rows*stride); }
  if(!dx)return;
  const int bytes=(dx<0?-dx:dx)/2;
  for(int y=0;y<height;++y) {
    auto* row=pixels+y*stride;
    if(dx>0) {
      if(bytes){memmove(row+bytes,row,stride-bytes);memset(row,255,bytes);}
      if(dx&1)for(int b=int(stride)-1;b>=0;--b)row[b]=uint8_t((row[b]<<4)|(b?row[b-1]>>4:15));
    } else {
      if(bytes){memmove(row,row+bytes,stride-bytes);memset(row+stride-bytes,255,bytes);}
      if((-dx)&1)for(size_t b=0;b<stride;++b)row[b]=uint8_t((row[b]>>4)|(b+1<stride?row[b+1]<<4:240));
    }
  }
}
} // namespace freeink
