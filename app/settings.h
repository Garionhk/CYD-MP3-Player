// ===========================================================================
// settings.h -- the player's stored preferences (NVS namespace "mp3cfg")
// ===========================================================================
// Same arrangement as the weather clock's settings: one struct, loaded once at
// boot, saved as a whole. Nothing here is compiled-in user data, so one binary
// suits every board and every owner.
//
#pragma once

#include <Arduino.h>
#include "board.h"

// What the time readout under the progress bar shows. Tapping it cycles.
enum TimeMode : uint8_t {
  TIME_ELAPSED   = 0,   // 1:23 / 4:05
  TIME_REMAINING = 1,   // -2:42
  TIME_BOTH      = 2,   // 1:23 ........ -2:42
  TIME_MODE_COUNT
};

enum Language : uint8_t { LANG_EN = 0, LANG_ZH = 1, LANG_COUNT };

enum RepeatMode : uint8_t {
  REPEAT_OFF = 0,       // stop after the last track
  REPEAT_ALL = 1,       // wrap around
  REPEAT_ONE = 2,       // the same track again
  REPEAT_MODE_COUNT
};

// One device the player has connected to before.
static const int BT_KNOWN_MAX = 5;
struct BtKnown {
  uint8_t addr[6];
  char    name[33];
};

struct Settings {
  // Panel -- defaults come from the board profile, the stored values win.
  uint16_t panel      = CYD_PANEL_DRIVER;   // CYD_PANEL_ST7789 / CYD_PANEL_ILI9341
  bool     invert     = CYD_TFT_INVERT;
  uint8_t  rotation   = CYD_ROTATION;       // 0/2 portrait, 1/3 landscape
  uint8_t  brightness = 100;                // backlight %, 10-100

  // Appearance
  uint8_t  theme      = 0;                  // index into theme.cpp's palettes
  uint8_t  skin       = 0;                  // SKIN_* in layouts.h
  uint8_t  style      = 0;                  // index into style.cpp's table
  uint8_t  lang       = LANG_EN;

  // Player
  uint8_t  volume     = 40;                 // 0-100
  uint16_t lastTrack  = 0;                  // resume point: track index...
  uint32_t lastPosMs  = 0;                  // ...and position in it
  uint8_t  timeMode   = TIME_ELAPSED;
  bool     shuffle    = false;
  uint8_t  repeat     = REPEAT_ALL;

  // Background: 0 = off, N = /bg/bgN.gif
  uint8_t  bgIndex    = 1;

  // Bluetooth. The library keeps its own copy of the address for paging at
  // boot; this one lets the discovery fallback recognise the speaker too, and
  // the name is what the screen shows while the link comes up.
  String   btName;
  uint8_t  btAddr[6] = { 0, 0, 0, 0, 0, 0 };

  // The devices this player has connected to, most recent first, so the owner
  // can move between a speaker and a car without pairing again. Fixed-size
  // rather than Strings: it is written from the Bluetooth task's small stack
  // and stored as one NVS blob.
  BtKnown  btKnown[BT_KNOWN_MAX];
  uint8_t  btKnownCount = 0;

  // WiFi, used only by upload mode. Entered on a phone through the hotspot's
  // page, never compiled in.
  String   wifiSsid;
  String   wifiPass;
};

extern Settings g_settings;

void settings_begin();
void settings_save();

// Just the resume point (track + position), written often while playing --
// kept separate so it does not rewrite every other key each time.
void settings_saveResume();

// The panel controller, alone. The boot gesture uses it on a device whose
// screen shows nothing, so it must not depend on anything else being valid.
void settings_savePanel();
