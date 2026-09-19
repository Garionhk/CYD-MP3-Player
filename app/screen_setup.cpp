// Player image only (firmware.h).
#ifndef CYD_UPLOADER

// ===========================================================================
// screen_setup.cpp -- Setup menu
// ===========================================================================
// One row per setting. Tapping a row changes it on the spot (values cycle, the
// screen redraws in the new theme / orientation / language) or opens its own
// screen. More rows than fit, so ▲ / ▼ page the list; the layout is sized from
// the screen and works in either orientation.

#include "ui.h"
#include "display.h"
#include "theme.h"
#include "style.h"
#include "settings.h"
#include "background.h"
#include "calibrate.h"
#include "storage.h"
#include "bt.h"
#include "audio.h"
#include "layouts.h"
#include "firmware.h"

enum Row : uint8_t {
  ROW_BLUETOOTH, ROW_UPLOAD, ROW_THEME, ROW_STYLE, ROW_LAYOUT, ROW_BACKGROUND, ROW_ORIENTATION,
  ROW_BRIGHTNESS, ROW_LANGUAGE, ROW_INVERT, ROW_PANEL, ROW_CALIBRATE, ROW_ABOUT,
  ROW_COUNT
};

static const Txt ROW_LABELS[ROW_COUNT] = {
  T_BLUETOOTH_SPEAKER, T_UPLOAD_MUSIC, T_THEME, T_STYLE, T_LAYOUT, T_BACKGROUND, T_ORIENTATION,
  T_BRIGHTNESS, T_LANGUAGE, T_INVERT, T_PANEL, T_CALIBRATE, T_ABOUT,
};

static const int ROW_H   = UI_ROW_H;
static const int ARROW_W = UI_PAGER_W;

static int  top = 0;                      // first visible row

// Under the finger right now (the press hook, below).
static int  litRow   = -1;                // a row drawn pressed
static int  litArrow = -1;                // 0 up, 1 down
static bool dragging = false;             // the brightness track has the finger

static int  visibleRows() { return (tft.height() - UI_HEADER_H) / ROW_H; }
static bool needArrows()  { return visibleRows() < ROW_COUNT; }
static int  rowWidth()    { return tft.width() - (needArrows() ? ARROW_W : 0); }
static Rect upRect()   { return { rowWidth(), UI_HEADER_H, ARROW_W, (tft.height() - UI_HEADER_H) / 2 }; }
static Rect downRect() {
  const int h = (tft.height() - UI_HEADER_H) / 2;
  return { rowWidth(), UI_HEADER_H + h, ARROW_W, tft.height() - UI_HEADER_H - h };
}

// What each row's value is, and therefore what tapping it will do (ui.h).
static RowValue rowValue(Row r) {
  switch (r) {
    case ROW_BLUETOOTH:
      return ui_nav(bt_speakerName().length() ? bt_speakerName() : String(tr(T_NONE)));
    case ROW_THEME:
      return ui_cycle(String(g_settings.lang == LANG_ZH ? theme().nameZh : theme().nameEn));
    case ROW_STYLE:
      return ui_cycle(String(g_settings.lang == LANG_ZH ? style().nameZh : style().nameEn));
    case ROW_LAYOUT: {
      static const Txt SKINS[SKIN_COUNT] = { T_SKIN_CLASSIC, T_SKIN_BIG, T_SKIN_LIST };
      return ui_cycle(SKINS[g_settings.skin % SKIN_COUNT]);
    }
    case ROW_BACKGROUND:
      if (bg_count() == 0) return ui_info(String(tr(T_NO_GIFS)));
      return g_settings.bgIndex ? ui_cycle("bg" + String(g_settings.bgIndex)) : ui_cycle(T_OFF);
    case ROW_ORIENTATION: {
      // Rotation 1 is this board's upright landscape (board.h); 3 is it flipped.
      static const Txt ROT[4] = { T_PORTRAIT, T_LANDSCAPE, T_PORTRAIT_FLIPPED, T_LANDSCAPE_FLIPPED };
      return ui_cycle(ROT[g_settings.rotation & 3]);
    }
    case ROW_BRIGHTNESS: return ui_sliderValue(g_settings.brightness, 100);
    case ROW_LANGUAGE:   return ui_cycle(T_LANG_NAME);
    case ROW_INVERT:     return ui_toggleValue(g_settings.invert);
    // A chevron, not a cycle: tapping asks before it restarts.
    case ROW_PANEL:      return ui_nav(String(display_panelName(g_settings.panel)));
    case ROW_CALIBRATE:  return ui_nav();
    case ROW_UPLOAD:     return ui_nav();
    case ROW_ABOUT: {
      char b[40];
      snprintf(b, sizeof(b), "v" FIRMWARE_VERSION "  %d  %u KB", storage_trackCount(), ESP.getFreeHeap() / 1024);
      return ui_info(b);
    }
    default: return ui_info(String());
  }
}

static Rect rowRect(int r) {
  return { 0, UI_HEADER_H + (r - top) * ROW_H, rowWidth(), ROW_H };
}

static void drawRow(int r) {
  if (r < top || r >= top + visibleRows()) return;
  ui_row(rowRect(r), r, ROW_LABELS[r], rowValue((Row)r));
}

static void drawRows() {
  for (int r = top; r < top + visibleRows(); r++) {
    if (r < ROW_COUNT) drawRow(r);
    else { const Rect e = rowRect(r); tft.fillRect(e.x, e.y, e.w, e.h, theme().bg); }
  }
}

static void enter() {
  litRow = litArrow = -1;
  dragging = false;
  tft.fillScreen(theme().bg);
  ui_header(T_SETUP);
  top = constrain(top, 0, max(0, ROW_COUNT - visibleRows()));
  drawRows();
  if (needArrows()) ui_pager(upRect(), downRect());
}

static void tick(uint32_t now) {
  static uint32_t last = 0;
  if (now - last > 2000) {                  // free heap and speaker state move
    last = now;
    drawRow(ROW_ABOUT);
    if (litRow != ROW_BLUETOOTH) drawRow(ROW_BLUETOOTH);   // not out from under a finger
  }
}

static void switchPanel() {
  g_settings.panel = (g_settings.panel == CYD_PANEL_ST7789) ? CYD_PANEL_ILI9341 : CYD_PANEL_ST7789;
  settings_save();
  delay(200);
  ESP.restart();
}

static void touch(TouchEvent ev, int x, int y) {
  if (ev != TOUCH_TAP) return;
  if (UI_BACK_RECT.contains(x, y)) { ui_go(SCR_PLAYER); return; }
  if (needArrows() && (upRect().contains(x, y) || downRect().contains(x, y))) {
    const int before = top;
    top += upRect().contains(x, y) ? -(visibleRows() - 1) : (visibleRows() - 1);
    top = constrain(top, 0, max(0, ROW_COUNT - visibleRows()));
    if (top != before) drawRows();
    return;
  }
  if (y < UI_HEADER_H || x >= rowWidth()) return;
  const int r = top + (y - UI_HEADER_H) / ROW_H;
  if (r < 0 || r >= ROW_COUNT) return;

  switch ((Row)r) {
    case ROW_BLUETOOTH:
      ui_go(SCR_BLUETOOTH);
      return;
    case ROW_UPLOAD:
      if (!firmware_uploaderPresent()) {
        ui_toast(T_UPLOADER_MISSING);
        return;
      }
      // Keep the exact spot in the song: upload mode ends in a restart.
      g_settings.lastTrack = audio_currentTrack();
      g_settings.lastPosMs = audio_positionMs();
      settings_saveResume();
      firmware_bootUploader();          // restarts
      return;
    case ROW_THEME:
      g_settings.theme = (g_settings.theme + 1) % theme_count();
      settings_save();
      enter();                              // every colour changes
      return;
    case ROW_STYLE:
      // A style lands on the palette it was designed against; Theme is still
      // the owner's to change afterwards.
      g_settings.style = (g_settings.style + 1) % style_count();
      g_settings.theme = style().defaultTheme % theme_count();
      settings_save();
      enter();                              // every shape and colour changes
      return;
    case ROW_LAYOUT:
      g_settings.skin = (g_settings.skin + 1) % SKIN_COUNT;
      break;
    case ROW_BACKGROUND:
      if (bg_count() == 0) return;
      // Off -> bg1 -> ... -> bgN -> off. The player screen starts it.
      g_settings.bgIndex = (g_settings.bgIndex >= bg_count()) ? 0 : g_settings.bgIndex + 1;
      break;
    case ROW_ORIENTATION: {
      // Landscape -> landscape flipped -> portrait -> portrait flipped.
      static const uint8_t NEXT[4] = { /*0*/ 2, /*1*/ 3, /*2*/ 1, /*3*/ 0 };
      g_settings.rotation = NEXT[g_settings.rotation & 3];
      display_setRotation(g_settings.rotation);
      settings_save();
      enter();
      return;
    }
    case ROW_LANGUAGE:
      g_settings.lang = (g_settings.lang + 1) % LANG_COUNT;
      settings_save();
      enter();
      return;
    case ROW_INVERT:
      g_settings.invert = !g_settings.invert;
      tft.invertDisplay(g_settings.invert);
      break;
    case ROW_PANEL:
      // The wrong controller shows nothing, so this must not happen by
      // accident -- and the owner should know the way back before they need
      // it: the 8 s boot hold (display.h).
      ui_confirm(T_PANEL_CONFIRM, T_PANEL_WARN1, T_PANEL_WARN2, T_RESTART, switchPanel);
      return;
    case ROW_CALIBRATE:
      calibrate_run();
      enter();
      return;
    default:
      return;
  }
  settings_save();
  drawRow(r);
}

// ---------------------------------------------------------------------------
// The finger while it is down
// ---------------------------------------------------------------------------
static int rowAt(int x, int y) {
  if (y < UI_HEADER_H || x >= rowWidth()) return -1;
  const int r = top + (y - UI_HEADER_H) / ROW_H;
  return (r < ROW_COUNT && r < top + visibleRows()) ? r : -1;
}

// Follow the finger along the brightness track, repainting only its half of
// the row. Floored at 10 % so the screen can never be turned off entirely.
static void brightnessAt(int x) {
  const Rect row = rowRect(ROW_BRIGHTNESS);
  const Rect t = ui_rowTrack(row);
  if (t.w <= 0) return;
  const int v = constrain((x - t.x) * 100 / t.w, 10, 100);
  if (v == g_settings.brightness) return;
  g_settings.brightness = v;
  display_setBacklight(v);
  ui_rowSlider(row, ROW_BRIGHTNESS, v, 100);
}

static void drawArrow(int which, bool pressed) {
  if (which == 0) ui_button(upRect(), ICON_UP, false, pressed);
  else            ui_button(downRect(), ICON_DOWN, false, pressed);
}

static bool press(PressPhase phase, int x, int y) {
  switch (phase) {
    case PRESS_DOWN: {
      if (needArrows() && (upRect().contains(x, y) || downRect().contains(x, y))) {
        litArrow = upRect().contains(x, y) ? 0 : 1;
        drawArrow(litArrow, true);
        return false;
      }
      const int r = rowAt(x, y);
      if (r == ROW_BRIGHTNESS) {
        dragging = true;
        brightnessAt(x);
        return true;                      // a drag: no tap follows
      }
      if (r >= 0 && rowValue((Row)r).kind != RK_INFO) {
        litRow = r;
        ui_row(rowRect(r), r, ROW_LABELS[r], rowValue((Row)r), true);
      }
      return false;
    }
    case PRESS_MOVE:
      if (dragging) brightnessAt(x);
      return dragging;
    case PRESS_UP:
      if (dragging) {
        dragging = false;
        settings_save();                  // once, when the finger lifts
        return true;
      }
      if (litArrow >= 0) { drawArrow(litArrow, false); litArrow = -1; }
      if (litRow >= 0)   { const int r = litRow; litRow = -1; drawRow(r); }
      return false;
  }
  return false;
}

const Screen SCREEN_SETUP = { enter, nullptr, tick, touch, press };

#endif  // !CYD_UPLOADER
