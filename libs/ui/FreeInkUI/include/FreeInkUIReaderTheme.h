#pragma once
#include <FreeInkUIDisplayTarget.h>
namespace freeink::ui {
// Shared reader metrics composed from existing SDK tokens and font slots.
inline void configureReaderFonts(DisplayTarget& target, RuntimeGlyphSource& source) {
  target.setFont(0,source,28,34,25);
  target.setFont(1,source,38,46,34);
  target.setFont(2,source,60,72,54);
  target.setGlyphFallback(&source);
}
inline ThemeTokens readerTheme() {
  auto theme = defaultThemeTokens(0,1,2);
  theme.headerHeight = 144; theme.footerHeight = 96; theme.rowHeight = 88;
  theme.minTouchSize = 64; theme.listTouchMinRowHeight = 88;
  theme.spaceXs = 4; theme.spaceSm = 8; theme.spaceMd = 12; theme.spaceLg = 24;
  theme.headerSidePadding = 24; theme.headerUnderline = 0;
  theme.button = flatButtonStyles(12);
  theme.button.selected.background = Paint::solid(Color::LightGray);
  theme.button.active.background = Paint::solid(Color::DarkGray);
  theme.button.active.foreground = Paint::solid(Color::White);
  return theme;
}
}
