// ===========================================================================
// layouts.h -- the Now Playing screen's skins, per orientation
// ===========================================================================
// A skin is data: where the background plays, where the band with the title,
// progress bar and time sits, and where each button goes. screen_player.cpp
// draws whatever the active table says, so a new skin is a new table and no
// drawing code.
//
// Every skin has a landscape (320x240) and a portrait (240x320) table.
// A rectangle with w == 0 is "not in this skin".
#pragma once

#include "geometry.h"
#include "ui.h"

enum Skin : uint8_t { SKIN_CLASSIC = 0, SKIN_BIG = 1, SKIN_LIST = 2, SKIN_COUNT };

enum PlayerButton : uint8_t {
  PB_SETUP, PB_PREV, PB_PLAY, PB_NEXT, PB_VOL_DOWN, PB_VOL_UP, PB_COUNT
};

static const int LAYOUT_MAX_MASKS = 6;

struct PlayerLayout {
  Rect area;                          // background animation (w 0: none)
  Rect band;                          // solid panel behind title / bar / time
  Rect title;                         // song title (tap: library)
  Rect status;                        // Bluetooth icon + volume
  Rect bar;                           // progress bar
  Rect barTouch;                      // taller touch target for the bar
  Rect time;                          // time readout (tap: cycle mode)
  Rect list;                          // track list (List skin only)
  int  listRowH;
  Rect listUp, listDown;              // page the list (List skin only)
  Rect buttons[PB_COUNT];
  Rect masks[LAYOUT_MAX_MASKS];       // what the background must not paint
  int  maskCount;
};

// The table for Settings::skin at the current rotation.
const PlayerLayout& layout_player();

bool layout_isPortrait();
