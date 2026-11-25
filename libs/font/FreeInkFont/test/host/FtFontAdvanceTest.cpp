#include "FtFont.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <vector>
using freeink::font::FtFont;
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  FILE* input=fopen(argv[1],"rb");if(!input)return 2;
  fseek(input,0,SEEK_END);const auto length=ftell(input);rewind(input);
  std::vector<uint8_t> data(length);
  if(fread(data.data(),1,data.size(),input)!=data.size())return 2;
  fclose(input);
  unsigned checked=0;
  for(unsigned variant=0;variant<3;++variant) {
    FtFont font;if(!font.init(data.data(),data.size(),16,variant==1?700:400,variant==2))return 3;
    for(auto hint:{FtFont::HintingMode::Default,FtFont::HintingMode::None,FtFont::HintingMode::Auto,FtFont::HintingMode::Light}) {
      FtFont::RenderOptions options;options.hinting=hint;
      if(!font.setRenderOptions(options))continue;
      for(unsigned size:{8,13,16,31,56,97,160}) {
        // Dense Latin, punctuation, and sampled CJK including composite glyphs.
        for(uint32_t cp=0;cp<0xa000;cp+=cp<512?1:17) {
          if(!font.hasGlyph(cp))continue;
          FtFont::GlyphMetrics metrics;
          if(!font.metrics26_6(cp,size*64,metrics))return 4;
          const auto expected=int16_t(std::clamp<int32_t>(metrics.advance26_6>>6,INT16_MIN,INT16_MAX));
          const auto actual=font.advance(cp,size,0);
          if(actual!=expected || font.lastGlyphFailure()!=FtFont::GlyphFailure::None) {
            fprintf(stderr,"advance mismatch %s cp=%x size=%u style=%u hint=%u expected=%d got=%d\n",argv[1],cp,size,variant,unsigned(hint),expected,actual);return 1;
          }
          ++checked;
        }
      }
    }
  }
  printf("Advance/full-metrics parity: %u glyphs (%s)\n",checked,argv[1]);
}
