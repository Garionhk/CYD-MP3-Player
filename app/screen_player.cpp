// Player image only (firmware.h).
#ifndef CYD_UPLOADER

// ===========================================================================
// screen_player.cpp -- Now Playing
// ===========================================================================
// Draws whatever the active skin table says (layouts.h): Classic with its
// background animation, Big buttons, or List with a mini-player, each in
// landscape or portrait.
//
// The band is a solid rectangle the background is masked out of, so the title,
// bar and time are drawn once and only redrawn when their content changes.
//
// Touch: buttons as drawn; long-press |<< / >>| seeks -/+10 s; tap the progress
// bar to jump there; tap the time to cycle elapsed / remaining / both; tap the
// title to open the library; in the List skin, tap a row to play it and use the
// arrows beside the list to page (hold one to jump to the start or end).

#include "ui.h"
#include "display.h"
#include "theme.h"
#include "audio.h"
#include "bt.h"
#include "background.h"
#include "storage.h"
#include "settings.h"
#include "titles.h"
#include "layouts.h"
#include "tracklist.h"

static const Icon BUTTON_ICONS[PB_COUNT] = {
  ICON_GEAR, ICON_PREV, ICON_PLAY, ICON_NEXT, ICON_VOL_DOWN, ICON_VOL_UP
};

static const PlayerLayout* L = nullptr;
static Rect bgView = { 0, 0, 0, 0 };   // L->area less the style's frame

static uint32_t shownSerial = 0xFFFFFFFF;
static String   title;
static int      titleW = 0, scrollX = 0;
static uint32_t scrollAt = 0;
static uint32_t shownSecond = 0xFFFFFFFF;
static int      shownBarPx = -1;
static bool     shownPlaying = false;
static int      shownVolume = -1;
static int      shownBt = -1;
static int      listTop = 0;
static uint32_t saveAt = 0;              // debounced NVS write, 0 = nothing pending

static const uint32_t SCROLL_PAUSE_MS = 1500;
static const uint32_t SCROLL_STEP_MS  = 40;
static const int      SCROLL_GAP      = 40;

static void saveSoon(uint32_t now) { saveAt = now + 3000; }

// Under the finger right now (the press hook, at the bottom).
static int  litButton   = -1;            // PB_* or one of the two below
static int  dragFromVol = -1;            // volume when a drag on the chip began
static int  dragFromX   = 0;
static const int LIT_LIST_UP = PB_COUNT, LIT_LIST_DOWN = PB_COUNT + 1;
// Finger travel for the whole 0-100 % range. Wider than the chip on purpose:
// the drag is relative and keeps the finger once it has landed, so it can run
// out over the band and the buttons.
static const int VOL_DRAG_SPAN = 120;

static bool has(const Rect& r) { return r.w > 0; }

static void drawArea() {
  if (!has(L->area) || bg_active()) return;      // the background owns this area
  const Palette& p = theme();
  // The part of the area above the band, when the band overlaps it.
  // bgView is L->area less the style's frame, so the frame survives this fill.
  const Rect& A = has(bgView) ? bgView : L->area;
  const int h = has(L->band) && L->band.y > A.y && L->band.y < A.y + A.h
                  ? L->band.y - A.y : A.h;
  tft.fillRect(A.x, A.y, A.w, h, p.bg);
  ui_icon(ICON_NOTE, A.x + A.w / 2, A.y + h / 2, ICON_XL, p.dim, p.bg);
}

static void drawTitle() {
  const Palette& p = theme();
  const Rect& T = L->title;
  // Reload every time: the list and library draw other strips in between.
  if (titles_load(title)) {
    const Rect strip = { T.x, T.y + (T.h - TITLE_STRIP_H) / 2, T.w, TITLE_STRIP_H };
    titles_draw(strip, titleW > T.w ? scrollX : 0, SCROLL_GAP, p.text, p.band);
    return;
  }
  // No strip (no font on the card): built-in font, ASCII only.
  tft.setViewport(T.x, T.y, T.w, T.h);
  tft.setTextColor(p.text, p.band);
  tft.setTextDatum(TL_DATUM);
  tft.setTextWrap(false);
  if (titleW <= T.w) {
    tft.fillRect(0, 0, T.w, T.h, p.band);
    tft.drawString(title, 0, 2, 2);
  } else {
    tft.drawString(title, -scrollX, 2, 2);
    const int gapX = titleW - scrollX;
    tft.fillRect(gapX, 0, SCROLL_GAP, T.h, p.band);
    tft.drawString(title, gapX + SCROLL_GAP, 2, 2);
  }
  tft.resetViewport();
}

static void drawStatus() {
  const Palette& p = theme();
  const uint16_t btCol = bt_connected() ? p.accent : (bt_connecting() ? p.warn : p.dim);
  ui_chip(L->status, ICON_BLUETOOTH, btCol, String(audio_volume()) + "%", p.band);
}

static void drawBar(int px) { ui_bar(L->bar, px, theme().band); }

static void drawTime() {
  const Palette& p = theme();
  const Rect& T = L->time;
  const uint32_t pos = audio_positionMs(), dur = audio_durationMs();
  const String track = String(audio_currentTrack() + 1) + "/" + String(storage_trackCount());
  String left, right;
  switch (g_settings.timeMode) {
    case TIME_ELAPSED:
      left = ui_time(pos) + " / " + ui_time(dur, dur > 0);
      right = track;
      break;
    case TIME_REMAINING:
      left = track;
      right = "-" + ui_time(dur > pos ? dur - pos : 0, dur > 0);
      break;
    default:
      left = ui_time(pos);
      right = "-" + ui_time(dur > pos ? dur - pos : 0, dur > 0);
      break;
  }
  tft.fillRect(T.x, T.y, T.w, T.h, p.band);
  tft.setTextColor(p.text, p.band);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(left, T.x + SP_M, T.cy(), ui_numericFont(left, T.h));
  tft.setTextColor(g_settings.timeMode == TIME_BOTH ? p.text : p.dim, p.band);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(right, T.x + T.w - SP_M, T.cy(), ui_numericFont(right, T.h));
  if (g_settings.timeMode == TIME_BOTH && T.w >= 180) {
    tft.setTextColor(p.dim, p.band);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(track, T.cx(), T.cy(), ui_numericFont(track, T.h));
  }
}

static void drawButton(int i, bool pressed = false) {
  if (!has(L->buttons[i])) return;
  Icon icon = BUTTON_ICONS[i];
  bool active = false;
  if (i == PB_PLAY) {
    active = audio_isPlaying();
    icon = active ? ICON_PAUSE : ICON_PLAY;
  }
  ui_button(L->buttons[i], icon, active, pressed);
}

static void drawList() {
  if (!has(L->list)) return;
  tracklist_draw(L->list, L->listRowH, listTop);
}

static void pageList(bool up) {
  const int rows = tracklist_rows(L->list, L->listRowH);
  const int before = listTop;
  listTop = tracklist_clampTop(L->list, L->listRowH, listTop + (up ? -(rows - 1) : rows - 1));
  if (listTop != before) drawList();
}

static void loadTitle() {
  title = storage_trackCount() ? storage_trackLabel(audio_currentTrack()) : String(tr(T_NO_MP3));
  titleW = titles_load(title) ? titles_width() : tft.textWidth(title, 2);
  scrollX = 0;
  scrollAt = millis() + SCROLL_PAUSE_MS;
}

static void enter() {
  L = &layout_player();
  litButton = dragFromVol = -1;
  const Palette& p = theme();
  tft.fillScreen(p.bg);

  if (has(L->area)) {
    // A style that frames the background gets its rule drawn here and the
    // animation kept clear of it -- the masks are static tables and cannot
    // carry a per-style rectangle.
    const int f = style().framePx;
    if (f) {
      for (int i = 0; i < f; i++)
        tft.drawRect(L->area.x + i, L->area.y + i, L->area.w - i * 2, L->area.h - i * 2, p.outline);
    }
    bgView = { L->area.x + f, L->area.y + f, L->area.w - f * 2, L->area.h - f * 2 };
    bg_setView(bgView, L->masks, L->maskCount);
    if (g_settings.bgIndex && !bg_active() && !bg_start(g_settings.bgIndex))
      g_settings.bgIndex = 0;
  } else {
    bg_stop();
  }
  drawArea();

  if (has(L->list)) {
    listTop = tracklist_topFor(L->list, L->listRowH, audio_currentTrack());
    drawList();
    ui_pager(L->listUp, L->listDown);
  }

  tft.fillRect(L->band.x, L->band.y, L->band.w, L->band.h, p.band);
  loadTitle();
  shownSerial = audio_trackSerial();
  drawTitle();
  drawStatus();
  shownBarPx = -1;
  shownSecond = 0xFFFFFFFF;

  for (int i = 0; i < PB_COUNT; i++) drawButton(i);
  shownPlaying = audio_isPlaying();
  shownVolume = audio_volume();
  shownBt = -1;
}

static void leave() {
  bg_stop();
}

static void tick(uint32_t now) {
  if (audio_trackSerial() != shownSerial) {
    shownSerial = audio_trackSerial();
    if (has(L->list)) {
      // Keep the playing track on the page and its highlight current.
      const int rows = tracklist_rows(L->list, L->listRowH);
      const int cur = audio_currentTrack();
      if (cur < listTop || cur >= listTop + rows)
        listTop = tracklist_topFor(L->list, L->listRowH, cur);
      drawList();
    }
    loadTitle();
    drawTitle();
    shownBarPx = -1;
    shownSecond = 0xFFFFFFFF;
  }

  if (titleW > L->title.w && now >= scrollAt) {
    scrollX += 2;
    if (scrollX >= titleW + SCROLL_GAP) {
      scrollX = 0;
      scrollAt = now + SCROLL_PAUSE_MS;
    } else {
      scrollAt = now + SCROLL_STEP_MS;
    }
    drawTitle();
  }

  const uint32_t pos = audio_positionMs(), dur = audio_durationMs();
  if (pos / 1000 != shownSecond) {
    shownSecond = pos / 1000;
    drawTime();
  }
  const int barPx = dur ? (int)((uint64_t)L->bar.w * pos / dur) : 0;
  if (barPx != shownBarPx) {
    shownBarPx = barPx;
    drawBar(barPx);
  }

  if (audio_isPlaying() != shownPlaying) {
    shownPlaying = audio_isPlaying();
    drawButton(PB_PLAY);
  }
  const int btState = bt_connected() ? 2 : (bt_connecting() ? 1 : 0);
  if (btState != shownBt || audio_volume() != shownVolume) {
    shownBt = btState;
    shownVolume = audio_volume();
    drawStatus();
  }

  if (saveAt && now >= saveAt) {
    saveAt = 0;
    settings_save();
  }

  if (has(L->area)) bg_tick(now);
}

static void touch(TouchEvent ev, int x, int y) {
  const uint32_t now = millis();
  const Rect* B = L->buttons;

  if (has(L->list)) {
    // Arrows page the list; a long press on one jumps to the start or end.
    if (L->listUp.contains(x, y) || L->listDown.contains(x, y)) {
      const bool up = L->listUp.contains(x, y);
      if (ev == TOUCH_LONG_PRESS) {
        const int before = listTop;
        listTop = tracklist_clampTop(L->list, L->listRowH, up ? 0 : storage_trackCount());
        if (listTop != before) drawList();
      } else if (ev == TOUCH_TAP) {
        pageList(up);
      }
      return;
    }
    if (L->list.contains(x, y)) {
      if (ev == TOUCH_TAP) {
        const int track = tracklist_hit(L->list, L->listRowH, listTop, x, y);
        if (track >= 0) audio_playTrack(track);
      }
      return;
    }
  }

  if (ev == TOUCH_LONG_PRESS) {
    if (B[PB_PREV].contains(x, y)) audio_seekRelative(-10);
    else if (B[PB_NEXT].contains(x, y)) audio_seekRelative(10);
    return;
  }
  if (ev != TOUCH_TAP) return;

  if (B[PB_SETUP].contains(x, y)) {
    ui_go(SCR_SETUP);
  } else if (L->title.contains(x, y)) {
    ui_go(SCR_LIBRARY);
  } else if (B[PB_PREV].contains(x, y)) {
    audio_prev();
  } else if (B[PB_PLAY].contains(x, y)) {
    audio_togglePause();
  } else if (B[PB_NEXT].contains(x, y)) {
    audio_next();
  } else if (B[PB_VOL_DOWN].contains(x, y) || B[PB_VOL_UP].contains(x, y)) {
    const int v = (int)audio_volume() + (B[PB_VOL_UP].contains(x, y) ? 5 : -5);
    audio_setVolume(constrain(v, 0, 100));
    g_settings.volume = audio_volume();
    saveSoon(now);
  } else if (L->barTouch.contains(x, y) && audio_durationMs() > 0) {
    const int frac = constrain(x - L->bar.x, 0, L->bar.w);
    const int64_t target = (int64_t)audio_durationMs() * frac / L->bar.w;
    audio_seekRelative((int)((target - (int64_t)audio_positionMs()) / 1000));
  } else if (L->time.contains(x, y)) {
    g_settings.timeMode = (g_settings.timeMode + 1) % TIME_MODE_COUNT;
    shownSecond = 0xFFFFFFFF;
    saveSoon(now);
  }
}

// ---------------------------------------------------------------------------
// The finger while it is down
// ---------------------------------------------------------------------------
static const Rect& litRect(int i) {
  return i == LIT_LIST_UP ? L->listUp : i == LIT_LIST_DOWN ? L->listDown : L->buttons[i];
}

static void drawLit(int i, bool pressed) {
  if (i < PB_COUNT) drawButton(i, pressed);
  else ui_button(litRect(i), i == LIT_LIST_UP ? ICON_UP : ICON_DOWN, false, pressed);
}

static bool press(PressPhase phase, int x, int y) {
  switch (phase) {
    case PRESS_DOWN:
      // The volume chip is a knob: drag it sideways. Relative to where the
      // finger lands, so touching it never jumps the volume.
      if (has(L->status) && L->status.contains(x, y)) {
        dragFromVol = audio_volume();
        dragFromX = x;
        return true;
      }
      for (int i = 0; i <= LIT_LIST_DOWN; i++) {
        if (has(litRect(i)) && litRect(i).contains(x, y)) {
          litButton = i;
          drawLit(i, true);
          break;
        }
      }
      return false;
    case PRESS_MOVE: {
      if (dragFromVol < 0) return false;
      const int v = constrain(dragFromVol + (x - dragFromX) * 100 / VOL_DRAG_SPAN, 0, 100);
      if (v != audio_volume()) {
        audio_setVolume(v);               // tick sees the change and redraws the chip
        g_settings.volume = audio_volume();
        saveSoon(millis());
      }
      return true;
    }
    case PRESS_UP:
      if (dragFromVol >= 0) { dragFromVol = -1; return true; }
      // Held buttons stay lit until release, even if the finger slides off:
      // the tap still lands where it went down, so un-lighting would lie.
      if (litButton >= 0) { drawLit(litButton, false); litButton = -1; }
      return false;
  }
  return false;
}

const Screen SCREEN_PLAYER = { enter, leave, tick, touch, press };

#endif  // !CYD_UPLOADER
