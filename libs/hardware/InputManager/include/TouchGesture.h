#pragma once
#include <stdint.h>

namespace freeink {
// Shared physical-pixel release policy for controllers, SDL and replay input.
// Apps decide direction/region after orientation and display calibration.
struct TouchGesture {
  static constexpr int swipePixels=48, tapSlop=swipePixels-1, holdSlop=28;
  static constexpr uint32_t swipeMs=1200, holdMs=500;
  enum class Result {None,Tap,Swipe};
  static int abs(int n){return n<0?-n:n;}
  static Result classify(int dx,int dy,uint32_t elapsed,bool moved,bool held=false) {
    if(held)return Result::None;
    if(abs(dx)>=swipePixels || abs(dy)>=swipePixels)
      return elapsed<=swipeMs?Result::Swipe:Result::None;
    return !moved?Result::Tap:Result::None;
  }
  void begin(int x,int y,uint32_t time){x0=x;y0=y;t0=time;maxDistance=0;held=false;}
  void sample(int x,int y,uint32_t time) {
    const int dx=abs(x-x0),dy=abs(y-y0);
    if(dx>maxDistance)maxDistance=dx;
    if(dy>maxDistance)maxDistance=dy;
    if(maxDistance<=holdSlop && uint32_t(time-t0)>=holdMs)held=true;
  }
  Result finish(int x,int y,uint32_t time) {
    sample(x,y,time);
    return classify(x-x0,y-y0,time-t0,maxDistance>tapSlop,held);
  }
  int x0=0,y0=0,maxDistance=0;uint32_t t0=0;bool held=false;
};
}
