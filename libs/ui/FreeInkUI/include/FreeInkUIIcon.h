#pragma once

// FreeInk SDK — freeink::Icon → FreeInkUI BitmapRef bridge (opt-in).
//
// The Icons library (libs/assets/Icons) generates antialiased 4-bpp icons at any
// size from Lucide SVGs via tools/gen_icons.py:
//
//   gen_icons.py --manifest icons.txt --svgdir libs/assets/Icons/lucide/icons \
//                --sizes 16,22,24,48 --out src/icons_gen.h
//
// This header adapts those structs to the BitmapRef every FreeInkUI component
// takes (Mask4 = 16 coverage levels, 0 transparent and 15 opaque). Opt-in like
// FreeInkUIGfxRenderer.h: only compilable in firmwares that also add the
// Icons library to lib_deps.

#include <Icon.h>

#include "FreeInkUI.h"

namespace freeink {
namespace ui {

inline BitmapRef bitmapFromIcon(const Icon& icon) {
  BitmapRef ref;
  ref.data = icon.bits;
  ref.width = icon.w;
  ref.height = icon.h;
  ref.format = BitmapFormat::Mask4;
  return ref;
}

}  // namespace ui
}  // namespace freeink
