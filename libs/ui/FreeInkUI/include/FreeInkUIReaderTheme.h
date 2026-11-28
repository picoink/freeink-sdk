#pragma once
#include <FreeInkUIDisplayTarget.h>
#include <FreeInkUI.h>
namespace freeink::ui {
// Shared reader metrics composed from existing SDK tokens and font slots.
inline void configureReaderFonts(DisplayTarget& target, RuntimeGlyphSource& source) {
  target.setFont(0,source,26,34,26);
  target.setFont(1,source,36,46,36);
  target.setFont(2,source,44,56,44);
  target.setFont(3,source,24,30,24);
  target.setFont(4,source,28,36,28);
  target.setFont(5,source,34,44,34);
  target.setFont(6,source,22,28,22);
  target.setFont(7,source,60,72,60);
  target.setGlyphFallback(&source);
}
inline ThemeTokens readerTheme() {
  auto theme = defaultThemeTokens(0,1,2);
  theme.headerHeight = 88; theme.footerHeight = 96; theme.rowHeight = 120;
  theme.minTouchSize = 64; theme.listTouchMinRowHeight = 88;
  theme.spaceXs = 4; theme.spaceSm = 8; theme.spaceMd = 12; theme.spaceLg = 28;
  theme.headerSidePadding = 0; theme.headerUnderline = 0;
  theme.button = outlinedButtonStyles(14,Color::LightGray);
  theme.button.normal.background = Paint::solid(Color::White);
  theme.button.normal.borderWidth = 1;
  theme.button.disabled.radius = theme.button.normal.radius;
  theme.listRow = selectedPlainListRowStyles();
  theme.listRow.selected.background = Paint::solid(Color::LightGray);
  theme.listRow.active = theme.listRow.selected;
  theme.button.selected.background = Paint::solid(Color::LightGray);
  theme.button.active.background = Paint::solid(Color::DarkGray);
  theme.button.active.foreground = Paint::solid(Color::White);
  return theme;
}
// Original PicoInk grouped-row treatment, composed with the SDK control.
template <class Screen>
void readerSettingRow(Screen& screen, Rect rect, const char* label, const char* detail,
                      BitmapRef icon, ActionId action, int16_t value = 0, bool enabled = true) {
  SettingRowProps row;
  row.label = label; row.subtitle = detail; row.icon = icon; row.iconSize = 40;
  row.action = action; row.valueId = value; row.enabled = enabled;
  row.labelText = screen.theme().bodyText; row.subtitleText = screen.theme().smallText;
  row.styles = screen.theme().listRow; row.sidePadding = 12; row.textGap = 16;
  row.titleSubtitleGap = 12; row.drawChevron = enabled && action != NO_ACTION; row.minTouchSize = 64;
  settingRow(screen.frame(),rect,row);
  screen.target().fill({rect.x,int16_t(rect.bottom()-1),rect.width,1},Paint::solid(Color::LightGray));
}

}
