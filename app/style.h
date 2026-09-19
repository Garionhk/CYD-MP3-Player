// ===========================================================================
// style.h -- how a control is shaped, chosen in Setup
// ===========================================================================
// Layouts (layouts.h) say WHERE a control is. Themes (theme.h) say what colour
// it is. A style says what SHAPE it takes: whether a button is a filled pill,
// a moulded key with a bevelled edge, an outline, a block with a hard shadow,
// or nothing at all but a hairline between touch cells.
//
// Shape used to live inside ui_button(), which meant a new look cost drawing
// code while a new layout cost none. A style is a row in the table in
// style.cpp, the same bargain skins already had.
//
// A style names a defaultTheme, applied when it is first chosen so it lands
// looking as intended; Theme stays a separate setting the owner can change
// afterwards.
#pragma once

#include <Arduino.h>

enum ButtonShape : uint8_t {
  BTN_FILL,      // filled rounded rect                      (Classic)
  BTN_PILL,      // raised surface, 1 px highlight on top    (Frosted deck)
  BTN_BEVEL,     // light top-left edge, dark bottom-right   (Hi-fi console)
  BTN_OUTLINE,   // border only, unfilled until active       (Terminal, Paper)
  BTN_SHADOW,    // outlined face over a hard offset block   (Cassette pop)
  BTN_NONE,      // no face; a hairline divides touch cells  (Swiss grid)
};

enum DividerStyle : uint8_t {
  DIV_ZEBRA,     // alternate rows carry the panel colour    (Classic)
  DIV_HAIRLINE,  // one subtle rule between rows
  DIV_BOX,       // every row framed
  DIV_NONE,      // nothing between rows
};

enum BarStyle : uint8_t {
  BAR_SOLID,     // a filled track                           (Classic)
  BAR_KNOB,      // filled, with a handle at the play head   (Frosted deck)
  BAR_SEGMENT,   // discrete cells, VU-meter fashion         (console, Terminal)
  BAR_DOTS,      // a run of dots                            (Paper mono)
};

struct Style {
  const char* nameEn;
  const char* nameZh;

  uint8_t defaultTheme;    // palette applied when this style is first chosen

  // Face geometry.
  uint8_t radius;          // corner radius, 0 = square
  uint8_t inset;           // face inset from the touch rectangle
  uint8_t border;          // outline thickness, 0 = none
  uint8_t shadow;          // hard offset block behind the face, 0 = none
  bool    highlight;       // 1 px lighter line along the face's top edge
  bool    bevel;           // light top-left and dark bottom-right edges

  ButtonShape  button;
  DividerStyle divider;
  BarStyle     bar;

  // TFT_eSPI font numbers. Chrome only: song titles come from 18 px strips
  // (titles.h) so that Chinese and English stay the same height, and a style
  // may not change that without re-rendering every cached strip.
  uint8_t fontBody;        // row labels, status text
  uint8_t fontNumeric;     // the time readout
  uint8_t fontCaption;     // secondary text

  // Pixels to shrink the background's view by, leaving room for a frame drawn
  // around it. The background's masks are static tables, so this is how a
  // style reserves space it can paint without being overwritten each frame.
  uint8_t framePx;
};

const Style& style();
int  style_count();
const Style& style_at(int i);
