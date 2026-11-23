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
  // Independent goldens from the scalar bitmap sampler, including clipping,
  // non-byte-aligned source widths, up/down-scaling and padded frame strides.
  constexpr uint64_t bitmapGolden[]={17635166816833864429ULL,6838975000517510862ULL,11413574799781121805ULL,9254966445807731614ULL};
  for (unsigned orientation=0;orientation<4;++orientation) {
    std::array<uint8_t,67*47> pixels; pixels.fill(37);
    std::array<uint8_t,16*64> bits;
    DisplayTarget target(pixels.data(),61,47,67,Orientation(orientation),true);
    uint32_t random=67890; uint64_t digest=14695981039346656037ULL;
    auto next=[&]() { random^=random<<13; random^=random>>17; random^=random<<5; return random; };
    for (auto& b:bits) b=next();
    for (unsigned n=0;n<500;++n) {
      target.setClipRect({int16_t(next()%30-8),int16_t(next()%30-8),int16_t(next()%65),int16_t(next()%65)});
      Rect r{int16_t(next()%65-10),int16_t(next()%65-10),int16_t(next()%65),int16_t(next()%65)};
      BitmapRef bitmap{bits.data(),uint16_t(1+next()%63),uint16_t(1+next()%63),BitmapFormat::Gray2,false};
      target.bitmap(r,bitmap,BitmapMode::Stretch);
      for (auto pixel:pixels) { digest^=pixel; digest*=1099511628211ULL; }
    }
    std::printf("bitmap %u: %llu\n",orientation,(unsigned long long)digest);
    if (bitmapGolden[orientation]) assert(digest==bitmapGolden[orientation]);
  }
  // Scalar runtime-glyph goldens: every alpha value, padded output rows,
  // negative bearings, clipping, overlap, colors and all orientations.
  struct Source : RuntimeGlyphSource {
    std::array<uint8_t,17*19> coverage{};
    Source() { for (size_t i=0;i<coverage.size();++i) coverage[i]=uint8_t(i*37); }
    bool glyph(uint32_t,uint16_t,Glyph* out) override {
      *out={coverage.data(),17,19,-2,-18,13}; return true;
    }
    int16_t advance(uint32_t,uint16_t) override { return 13; }
  } source;
  constexpr uint64_t glyphGolden[]={4097008610162319182ULL,16055920395469691003ULL,4620365512274218134ULL,1006011094707288379ULL};
  for (unsigned orientation=0;orientation<4;++orientation) {
    std::array<uint8_t,67*47> pixels; pixels.fill(37);
    DisplayTarget target(pixels.data(),61,47,67,Orientation(orientation),true);
    target.setFont(0,source,18,23,19);
    uint32_t random=98765; uint64_t digest=14695981039346656037ULL;
    auto next=[&]() { random^=random<<13; random^=random>>17; random^=random<<5; return random; };
    for (unsigned n=0;n<200;++n) {
      target.setClipRect({int16_t(next()%30-8),int16_t(next()%30-8),int16_t(next()%65),int16_t(next()%65)});
      Rect r{int16_t(next()%65-10),int16_t(next()%65-10),48,46};
      const Color colors[]={Color::Black,Color::DarkGray,Color::LightGray,Color::White,Color::Transparent};
      TextStyle style; style.color=colors[next()%5]; style.inverted=next()%2;
      target.text(r,"Ag",style);
      for (auto pixel:pixels) { digest^=pixel; digest*=1099511628211ULL; }
    }
    std::printf("glyph %u: %llu\n",orientation,(unsigned long long)digest);
    if (glyphGolden[orientation]) assert(digest==glyphGolden[orientation]);
  }
}
