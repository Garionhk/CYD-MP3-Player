// Player image only (firmware.h).
#ifndef CYD_UPLOADER

// ===========================================================================
// screen_library.cpp -- the song list, shuffle and repeat
// ===========================================================================
//   [< Library 13          ][shuffle][repeat]
//   |  1  傳說 - 林子祥                  | ^ |
//   |  2  別人的歌                       |   |
//   |  ...the playing one lit            | v |
//
// Sized from the screen, so it works in either orientation: 6 rows landscape,
// 8 portrait. Opened by tapping the song title on the player.

#include "ui.h"
#include "display.h"
#include "theme.h"
#include "audio.h"
#include "storage.h"
#include "settings.h"
#include "tracklist.h"

static const int ROW_H   = UI_ROW_H;
static const int ARROW_W = UI_PAGER_W;

static Rect listRect()    { return { 0, UI_HEADER_H, tft.width() - ARROW_W, tft.height() - UI_HEADER_H }; }
static Rect shuffleRect() { return { tft.width() - 98, 0, 49, UI_HEADER_H }; }
static Rect repeatRect()  { return { tft.width() - 49, 0, 49, UI_HEADER_H }; }
static Rect upRect() {
  const Rect l = listRect();
  return { l.w, l.y, ARROW_W, l.h / 2 };
}
static Rect downRect() {
  const Rect l = listRect();
  return { l.w, l.y + l.h / 2, ARROW_W, l.h - l.h / 2 };
}

static int top = 0;
static int shownTrack = -1;

static void drawHeaderButtons() {
  const uint16_t band = theme().band;
  ui_iconToggle(shuffleRect(), ICON_SHUFFLE, audio_shuffle(), band);
  const uint8_t rep = audio_repeat();
  ui_iconToggle(repeatRect(), rep == REPEAT_ONE ? ICON_REPEAT_ONE : ICON_REPEAT,
                rep != REPEAT_OFF, band);
}

static void enter() {
  tft.fillScreen(theme().bg);
  ui_header(T_LIBRARY, String(storage_trackCount()));
  drawHeaderButtons();
  top = tracklist_topFor(listRect(), ROW_H, audio_currentTrack());
  shownTrack = audio_currentTrack();
  tracklist_draw(listRect(), ROW_H, top);
  ui_pager(upRect(), downRect());
}

static void tick(uint32_t) {
  // The playing track moved (end of song): move the highlight with it.
  if (audio_currentTrack() != shownTrack) {
    const Rect l = listRect();
    const int rows = tracklist_rows(l, ROW_H);
    const int old = shownTrack;
    shownTrack = audio_currentTrack();
    if (old >= top && old < top + rows) tracklist_drawRow(l, ROW_H, top, old - top);
    if (shownTrack >= top && shownTrack < top + rows)
      tracklist_drawRow(l, ROW_H, top, shownTrack - top);
  }
}

static void touch(TouchEvent ev, int x, int y) {
  if (ev != TOUCH_TAP && ev != TOUCH_LONG_PRESS) return;
  if (UI_BACK_RECT.contains(x, y)) { ui_go(SCR_PLAYER); return; }

  if (shuffleRect().contains(x, y)) {
    audio_setShuffle(!audio_shuffle());
    g_settings.shuffle = audio_shuffle();
    settings_save();
    drawHeaderButtons();
    return;
  }
  if (repeatRect().contains(x, y)) {
    // All -> One -> Off -> All
    const uint8_t next = audio_repeat() == REPEAT_ALL ? REPEAT_ONE
                       : audio_repeat() == REPEAT_ONE ? REPEAT_OFF : REPEAT_ALL;
    audio_setRepeat(next);
    g_settings.repeat = next;
    settings_save();
    drawHeaderButtons();
    return;
  }
  const Rect l = listRect();
  // A long press on the arrows jumps to the very start / end.
  if (upRect().contains(x, y) || downRect().contains(x, y)) {
    const bool up = upRect().contains(x, y);
    const int rows = tracklist_rows(l, ROW_H);
    const int before = top;
    if (ev == TOUCH_LONG_PRESS) top = up ? 0 : storage_trackCount();
    else top += up ? -(rows - 1) : (rows - 1);
    top = tracklist_clampTop(l, ROW_H, top);
    if (top != before) tracklist_draw(l, ROW_H, top);
    return;
  }
  if (ev == TOUCH_TAP) {
    const int track = tracklist_hit(l, ROW_H, top, x, y);
    if (track >= 0) {
      audio_playTrack(track);
      ui_go(SCR_PLAYER);
    }
  }
}

const Screen SCREEN_LIBRARY = { enter, nullptr, tick, touch };

#endif  // !CYD_UPLOADER
