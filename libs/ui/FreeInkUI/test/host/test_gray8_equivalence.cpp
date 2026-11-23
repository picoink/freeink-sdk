// Golden scenes captured from the scalar renderer before optimization.
#include <FreeInkUIDisplayTarget.h>
#include <array>
#include <cassert>
#include <cstdio>
using namespace freeink::ui;
int main() {
  constexpr uint64_t golden[] = {5597993389776442870ULL,4368237096470293838ULL,13591335326007868942ULL,7583761200910116854ULL};
  for (unsigned orientation=0;orientation<4;++orientation) {
    std::array<uint8_t,67*47> pixels; pixels.fill(37);
    DisplayTarget target(pixels.data(),61,47,67,Orientation(orientation),true);
    uint32_t random=12345; uint64_t digest=14695981039346656037ULL;
    auto next=[&]() { random^=random<<13; random^=random>>17; random^=random<<5; return random; };
    for (unsigned n=0;n<500;++n) {
      target.setClipRect({int16_t(next()%30-8),int16_t(next()%30-8),int16_t(next()%65),int16_t(next()%65)});
      Rect r{int16_t(next()%65-10),int16_t(next()%65-10),int16_t(next()%65),int16_t(next()%65)};
      const Color colors[]={Color::Black,Color::DarkGray,Color::LightGray,Color::White,Color::Transparent};
      Paint paint=Paint::solid(colors[next()%5]); uint8_t radius=next()%40, corners=next()%16;
      if (n%2) target.fill(r,paint,radius,corners); else target.stroke(r,paint,1+next()%8,radius,corners);
      for (auto pixel:pixels) { digest^=pixel; digest*=1099511628211ULL; }
    }
    std::printf("%u: %llu\n",orientation,(unsigned long long)digest);
    if (golden[orientation]) assert(digest==golden[orientation]);
  }
}
