// ===========================================================================
// i18n.h -- every fixed piece of UI text, in English and Traditional Chinese
// ===========================================================================
// tr(T_X) returns the string for the current language. Chinese strings are
// drawn from strips pre-rendered at boot (titles.h) -- the same pipeline as
// song titles -- so the panel needs no CJK font in flash. When the font is not
// on the card the strip is missing and ui_text() falls back to English.
//
// Boot screens that run before the SD card is mounted stay in English: there
// is nothing to draw Chinese with yet.
#pragma once

#include <Arduino.h>

enum Txt : uint16_t {
  T_SETUP, T_BLUETOOTH_SPEAKER, T_THEME, T_LAYOUT, T_STYLE, T_BACKGROUND, T_ORIENTATION,
  T_BRIGHTNESS, T_LANGUAGE, T_INVERT, T_PANEL, T_CALIBRATE, T_ABOUT,
  T_ON, T_OFF, T_NONE, T_OFFLINE, T_NO_GIFS,
  T_LANDSCAPE, T_LANDSCAPE_FLIPPED, T_PORTRAIT, T_PORTRAIT_FLIPPED,
  T_SKIN_CLASSIC, T_SKIN_BIG, T_SKIN_LIST,
  T_LANG_NAME,
  T_BLUETOOTH, T_PAIR_NEW, T_CONNECTED, T_CONNECTING, T_PAIRING_HINT,
  T_LOOKING_FOR, T_PAIRING_MODE, T_SEARCHING,
  T_LIBRARY, T_NO_MP3,
  T_CAL_TITLE, T_CAL_PRESS_EACH, T_CAL_TL, T_CAL_TR, T_CAL_BR, T_CAL_BL, T_CAL_HOLD,
  T_CAL_CONFIRM, T_CAL_SKIPPED, T_CAL_KEEPING, T_CAL_DONE, T_CAL_NOT_QUITE,
  T_CAL_TRY_AGAIN, T_CAL_ROUGH, T_CAL_REDO,
  T_UPLOAD_MUSIC, T_UPLOAD_MODE, T_OPEN_BROWSER, T_JOIN_WIFI, T_THEN_OPEN,
  T_CONNECTING_WIFI, T_WIFI_FAILED, T_DONE, T_HOTSPOT, T_RECEIVING, T_RESTARTING,
  T_CANCEL, T_RESTART, T_PANEL_CONFIRM, T_PANEL_WARN1, T_PANEL_WARN2, T_UPLOADER_MISSING,
  T_COUNT
};

const char* tr(Txt id);
const char* tr_en(Txt id);            // always English (logs)

// Every Chinese string, for pre-rendering. `fn` is called once per string.
void i18n_forEachChinese(void (*fn)(const char* text));
