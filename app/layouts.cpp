// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "layouts.h"
#include "display.h"
#include "settings.h"

// Order of buttons[]: SETUP, PREV, PLAY, NEXT, VOL_DOWN, VOL_UP.
static const Rect NONE = { 0, 0, 0, 0 };

// ---------------------------------------------------------------------------
// Classic: background beside (landscape) or above (portrait) the controls.
// ---------------------------------------------------------------------------
//   landscape                         portrait
//   +-----------------------+----+    +------------------+
//   | background 240x240    | ⚙  |    | background       |
//   |                       | |<<|    | 240x200          |
//   |                       | >||    +------------------+
//   |                       | >>||    | title        BT %|
//   |[title   BT %]         | -  |    | ====bar========= |
//   |[====bar=====]         | +  |    | 1:23 / 4:05  3/13|
//   |[1:23 / 4:05 ]         |    |    +------------------+
//   +-----------------------+----+    |<< >|| >>| - + ⚙ |
static const PlayerLayout CLASSIC_LAND = {
  /*area*/ { 0, 0, 240, 240 }, /*band*/ { 0, 176, 240, 64 },
  /*title*/ { 6, 178, 170, 20 }, /*status*/ { 178, 178, 60, 20 },
  /*bar*/ { 8, 204, 224, 6 }, /*barTouch*/ { 0, 198, 240, 16 },
  /*time*/ { 0, 214, 240, 26 }, /*list*/ NONE, 0, /*listUp*/ NONE, /*listDown*/ NONE,
  /*buttons*/ { { 240, 0, 80, 40 }, { 240, 40, 80, 40 }, { 240, 80, 80, 40 },
                { 240, 120, 80, 40 }, { 240, 160, 80, 40 }, { 240, 200, 80, 40 } },
  /*masks*/ { { 0, 176, 240, 64 } }, 1,
};

static const PlayerLayout CLASSIC_PORT = {
  /*area*/ { 0, 0, 240, 200 }, /*band*/ { 0, 200, 240, 60 },
  /*title*/ { 6, 202, 170, 20 }, /*status*/ { 178, 202, 60, 20 },
  /*bar*/ { 8, 226, 224, 6 }, /*barTouch*/ { 0, 220, 240, 16 },
  /*time*/ { 0, 234, 240, 26 }, /*list*/ NONE, 0, /*listUp*/ NONE, /*listDown*/ NONE,
  /*buttons*/ { { 200, 260, 40, 60 }, { 0, 260, 40, 60 }, { 40, 260, 40, 60 },
                { 80, 260, 40, 60 }, { 120, 260, 40, 60 }, { 160, 260, 40, 60 } },
  /*masks*/ {}, 0,
};

// ---------------------------------------------------------------------------
// Big buttons: no background, controls you can hit from across the room.
// ---------------------------------------------------------------------------
static const PlayerLayout BIG_LAND = {
  /*area*/ NONE, /*band*/ { 0, 0, 320, 64 },
  /*title*/ { 6, 2, 250, 20 }, /*status*/ { 258, 2, 60, 20 },
  /*bar*/ { 8, 28, 304, 6 }, /*barTouch*/ { 0, 22, 320, 16 },
  /*time*/ { 0, 38, 320, 26 }, /*list*/ NONE, 0, /*listUp*/ NONE, /*listDown*/ NONE,
  /*buttons*/ { { 107, 152, 106, 88 }, { 0, 64, 107, 88 }, { 107, 64, 106, 88 },
                { 213, 64, 107, 88 }, { 0, 152, 107, 88 }, { 213, 152, 107, 88 } },
  /*masks*/ {}, 0,
};

static const PlayerLayout BIG_PORT = {
  /*area*/ NONE, /*band*/ { 0, 0, 240, 64 },
  /*title*/ { 6, 2, 170, 20 }, /*status*/ { 178, 2, 60, 20 },
  /*bar*/ { 8, 28, 224, 6 }, /*barTouch*/ { 0, 22, 240, 16 },
  /*time*/ { 0, 38, 240, 26 }, /*list*/ NONE, 0, /*listUp*/ NONE, /*listDown*/ NONE,
  /*buttons*/ { { 120, 149, 120, 85 }, { 0, 64, 120, 85 }, { 0, 149, 120, 85 },
                { 120, 64, 120, 85 }, { 0, 234, 120, 86 }, { 120, 234, 120, 86 } },
  /*masks*/ {}, 0,
};

// ---------------------------------------------------------------------------
// List: the track list fills the screen, a mini-player underneath.
// ---------------------------------------------------------------------------
static const PlayerLayout LIST_LAND = {
  /*area*/ NONE, /*band*/ { 0, 174, 320, 66 },
  /*title*/ { 6, 176, 246, 20 }, /*status*/ { 254, 176, 64, 20 },
  /*bar*/ { 8, 198, 304, 4 }, /*barTouch*/ { 0, 194, 320, 12 },
  /*time*/ { 0, 204, 110, 36 }, /*list*/ { 0, 0, 276, 174 }, 29,
  /*listUp*/ { 276, 0, 44, 87 }, /*listDown*/ { 276, 87, 44, 87 },
  /*buttons*/ { { 285, 204, 35, 36 }, { 110, 204, 35, 36 }, { 145, 204, 35, 36 },
                { 180, 204, 35, 36 }, { 215, 204, 35, 36 }, { 250, 204, 35, 36 } },
  /*masks*/ {}, 0,
};

static const PlayerLayout LIST_PORT = {
  /*area*/ NONE, /*band*/ { 0, 203, 240, 117 },
  /*title*/ { 6, 205, 170, 20 }, /*status*/ { 178, 205, 60, 20 },
  /*bar*/ { 8, 229, 224, 5 }, /*barTouch*/ { 0, 223, 240, 16 },
  /*time*/ { 0, 237, 240, 24 }, /*list*/ { 0, 0, 196, 203 }, 29,
  /*listUp*/ { 196, 0, 44, 101 }, /*listDown*/ { 196, 101, 44, 102 },
  /*buttons*/ { { 200, 261, 40, 59 }, { 0, 261, 40, 59 }, { 40, 261, 40, 59 },
                { 80, 261, 40, 59 }, { 120, 261, 40, 59 }, { 160, 261, 40, 59 } },
  /*masks*/ {}, 0,
};

static const PlayerLayout* TABLE[SKIN_COUNT][2] = {
  { &CLASSIC_LAND, &CLASSIC_PORT },
  { &BIG_LAND,     &BIG_PORT },
  { &LIST_LAND,    &LIST_PORT },
};

bool layout_isPortrait() { return (tft.getRotation() & 1) == 0; }

const PlayerLayout& layout_player() {
  return *TABLE[g_settings.skin % SKIN_COUNT][layout_isPortrait() ? 1 : 0];
}

#endif  // !CYD_UPLOADER
