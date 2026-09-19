// ===========================================================================
// theme.h -- colour palettes, chosen in Setup
// ===========================================================================
// Screens never use a colour literal; they ask theme(), which follows
// Settings::theme. Adding a theme is adding a row to the table in theme.cpp.
//
// The roles below are named for what they mean, not what they look like, so a
// palette can invert without a screen knowing. Read the surfaces as a stack:
//
//   bg             surface 0 -- the screen itself
//   band           surface 1 -- a panel, the header, an alternate row
//   surfaceRaised  surface 2 -- a face that has to read as lifted off the panel
//
// A style (style.h) decides which of them a given control is painted with.
#pragma once

#include <Arduino.h>

struct Palette {
  const char* nameEn;
  const char* nameZh;

  // Surfaces, lowest first.
  uint16_t bg, band, surfaceRaised;

  // Text on those surfaces. `disabled` is for a control that is present but
  // cannot be used -- dimmer than `dim`, which is merely secondary.
  uint16_t text, dim, disabled;

  // Lines. `outline` is a border you are meant to see; `outlineSubtle` is a
  // divider you are not supposed to notice.
  uint16_t outline, outlineSubtle;

  // Controls. `pressed` is the face under a finger, between `btn` and
  // `btnActive` in weight, so a tap is acknowledged before it lands.
  uint16_t btn, btnText, btnActive, onControlActive, pressed;

  // The highlight colour, and what stays legible on top of it.
  uint16_t accent, onAccent;

  // Progress bar.
  uint16_t barBg, bar;

  // Status.
  uint16_t good, warn, bad;
};

const Palette& theme();
int  theme_count();
const Palette& theme_at(int i);
