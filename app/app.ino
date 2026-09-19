// ===========================================================================
// CYD Bluetooth MP3 Player -- application (ESP32-2432S028R, 2.8")
// ===========================================================================
// MP3s from the SD card, played to a Bluetooth speaker, with a touch UI and an
// animated background. Every piece of hardware behaviour this relies on was
// proven first in stage0/ -- see docs/stage0_results.md for the numbers.
//
//   Display  HSPI      TFT_eSPI, ST7789 or ILI9341 (runtime switch, display.h)
//   SD card  VSPI      music, backgrounds, converted frames
//   Touch    bit-bang  XPT2046 (touch.h)
//   Audio    Bluetooth A2DP source (bt.h) fed by the decode task (audio.h)
//
// Boot order matters for memory: rendering song titles (~100 KB of font
// metrics) and converting backgrounds (~30 KB of GIF decoder) both run BEFORE
// Bluetooth takes its ~80 KB.
//
// This sketch builds TWO firmware images (firmware.h): the player, and with
// CYD_UPLOADER defined, the WiFi uploader. tools/flash.sh builds and flashes
// both; setup() below is shared up to the point where they part ways.

#include <SD.h>
#include <TFT_eSPI.h>

#include "board.h"
#include "settings.h"
#include "display.h"
#include "touch.h"
#include "calibrate.h"
#include "theme.h"
#include "storage.h"
#include "background.h"
#include "audio.h"
#include "bt.h"
#include "ui.h"
#include "titles.h"
#include "uploadmode.h"
#include "firmware.h"

// The loop task (which also runs setup: calibration, title rendering) measured
// 2.8 KB of its default 8 KB stack in use. 5 KB keeps a wide margin and hands
// 3 KB back to a heap Bluetooth leaves short.
SET_LOOP_TASK_STACK_SIZE(5 * 1024);

#ifdef CYD_UPLOADER
static const char* FIRMWARE_LABEL = FIRMWARE_VERSION " uploader";
#else
static const char* FIRMWARE_LABEL = FIRMWARE_VERSION;
#endif

// ---------------------------------------------------------------------------
// Compile-time cross-checks: board.h vs TFT_eSPI's User_Setup.h
// ---------------------------------------------------------------------------
// Each of these is a mistake with no runtime error -- a dead panel, or an SD
// card that will not mount -- so it is worth stopping the build for.
#ifndef USE_HSPI_PORT
  #error "User_Setup.h is missing '#define USE_HSPI_PORT' -- the display would take VSPI, which the SD card needs. Copy config/User_Setup_2432S028R_ST7789.h.template into the TFT_eSPI library."
#endif
#if CYD_PANEL_DRIVER == CYD_PANEL_ST7789
  #if !defined(ST7789_DRIVER) && !defined(ST7789_2_DRIVER)
    #error "board.h selects an ST7789 panel but TFT_eSPI's User_Setup.h does not define ST7789_DRIVER. Copy config/User_Setup_2432S028R_ST7789.h.template into the TFT_eSPI library."
  #endif
#elif CYD_PANEL_DRIVER == CYD_PANEL_ILI9341
  #if !defined(ILI9341_DRIVER) && !defined(ILI9341_2_DRIVER)
    #error "board.h selects an ILI9341 panel but TFT_eSPI's User_Setup.h does not define it. Copy config/User_Setup_2432S028R.h.template into the TFT_eSPI library."
  #endif
#endif
#if TFT_WIDTH != CYD_PANEL_W || TFT_HEIGHT != CYD_PANEL_H
  #error "User_Setup.h's TFT_WIDTH/TFT_HEIGHT disagree with board.h."
#endif
#if TFT_MOSI != CYD_TFT_MOSI_PIN || TFT_SCLK != CYD_TFT_SCLK_PIN || \
    TFT_CS != CYD_TFT_CS_PIN || TFT_DC != CYD_TFT_DC_PIN
  #error "User_Setup.h's TFT pins disagree with board.h."
#endif

// ---------------------------------------------------------------------------

static void centreMessage(const char* line1, const char* line2, uint16_t colour) {
  const Palette& p = theme();
  tft.fillScreen(p.bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(colour, p.bg);
  tft.drawString(line1, tft.width() / 2, tft.height() / 2 - 14, 4);
  if (line2) {
    tft.setTextColor(p.dim, p.bg);
    tft.drawString(line2, tft.width() / 2, tft.height() / 2 + 18, 2);
  }
}

static void libraryProgress(int done, int total) {
  const Palette& p = theme();
  if (done == 1) centreMessage("CYD MP3", "Reading song tags", p.accent);
  tft.fillRect(0, tft.height() / 2 + 34, tft.width(), 20, p.bg);
  tft.setTextColor(p.dim, p.bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(String(done) + " / " + String(total), tft.width() / 2, tft.height() / 2 + 44, 2);
}

static void titleProgress(int done, int total) {
  const Palette& p = theme();
  if (done == 1) centreMessage("CYD MP3", "Preparing song titles", p.accent);
  tft.fillRect(0, tft.height() / 2 + 34, tft.width(), 20, p.bg);
  tft.setTextColor(p.dim, p.bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(String(done) + " / " + String(total), tft.width() / 2, tft.height() / 2 + 44, 2);
}

static void conversionProgress(int n, int total, int frame) {
  const Palette& p = theme();
  if (frame == 0) {
    char b[40];
    snprintf(b, sizeof(b), "Preparing background %d of %d", n, total);
    centreMessage("CYD MP3", b, p.accent);
  }
  tft.fillRect(0, tft.height() / 2 + 34, tft.width(), 20, p.bg);
  tft.setTextColor(p.dim, p.bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(frame ? "frame " + String(frame) : String("reading GIF..."),
                 tft.width() / 2, tft.height() / 2 + 44, 2);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== CYD MP3 player %s ===\n", FIRMWARE_LABEL);
  cydPrintBanner();

  settings_begin();
  cydRgbLedOff();
  display_begin();
  touch_begin();
  display_bootGesture();                 // blind panel-controller switch, 8 s hold

  centreMessage("CYD MP3", FIRMWARE_LABEL, theme().accent);
  display_setBacklight(g_settings.brightness);

  if (!touch_hasStoredCalibration()) calibrate_run();

#ifdef CYD_UPLOADER
  // The uploader image: WiFi file manager only. Does not return.
  while (!storage_mount()) {
    centreMessage("No SD card", "insert a FAT32 card", theme().warn);
    delay(2000);
  }
  uploadmode_run();
}

void loop() {}

#else  // the player image

  while (!storage_begin(libraryProgress)) {
    centreMessage("No SD card", "insert a FAT32 card with /music", theme().warn);
    delay(2000);
  }

  // Both need RAM only available before Bluetooth starts: see the header.
  titles_prepareAll(titleProgress);
  bg_convertAll(conversionProgress);
  Serial.printf("heap before audio + bt: %u  largest %u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Resume where the last session stopped. A card whose library changed may
  // have a different song at that index -- the position is then meaningless,
  // so only the index is kept.
  const bool resumable = g_settings.lastTrack < storage_trackCount();
  audio_begin(resumable ? g_settings.lastTrack : 0, resumable ? g_settings.lastPosMs : 0,
              g_settings.volume, g_settings.shuffle, g_settings.repeat);
  bt_begin();

  const bool needSpeaker = bt_pairingBoot() || bt_speakerName().isEmpty();
  ui_go(needSpeaker ? SCR_BLUETOOTH : SCR_PLAYER);
}

// Keep the resume point current: at once when the track changes, every 30 s
// while playing, and when playback pauses.
static void persistResume(uint32_t now) {
  static uint32_t lastSave = 0;
  static bool     wasPlaying = true;
  if (audio_trackSerial() == 0) return;          // start track not open yet
  const int      track = audio_currentTrack();
  const uint32_t pos = audio_positionMs();
  const bool     playing = audio_isPlaying();
  bool save = false;
  if (track != g_settings.lastTrack) save = true;
  else if (playing && now - lastSave >= 30000) save = true;
  else if (!playing && wasPlaying) save = true;
  wasPlaying = playing;
  if (!save) return;
  g_settings.lastTrack = track;
  g_settings.lastPosMs = pos;
  settings_saveResume();
  lastSave = now;
}

void loop() {
  const uint32_t now = millis();

  bt_tick();
  audio_tick();
  persistResume(now);

  int x = 0, y = 0;
  const TouchEvent ev = touch_poll(&x, &y);
  if (ev == TOUCH_RECALIBRATE) {
    calibrate_run();
    ui_redraw();
  } else if (ev != TOUCH_NONE) {
    ui_touch(ev, x, y);
  }

  ui_tick(now);

  static uint32_t lastStat = 0;
  if (now - lastStat >= 10000) {
    uint32_t frames, avgMs, maxMs, maxBytes;
    bg_takeStats(frames, avgMs);
    bg_takeMax(maxMs, maxBytes);
    Serial.printf("diag: sd read max %u ms  bg frame max %u ms %u KB\n",
                  audio_takeMaxReadMs(), maxMs, maxBytes / 1024);
    Serial.printf("stat: heap %u min %u largest %u  stacks dec %u loop %u  buf %u%% underruns %u req %d  "
                  "bg %d %u.%u fps %u ms  %s  %s\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                  audio_decodeStackFree(), (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                  audio_bufferPct(), audio_underruns(), audio_maxRequest(),
                  bg_current(), frames * 1000 / (now - lastStat),
                  (frames * 10000 / (now - lastStat)) % 10, avgMs,
                  bt_connected() ? "bt" : "no-bt",
                  audio_isPlaying() ? "playing" : "paused");
    lastStat = now;
  }
  delay(2);
}

#endif  // CYD_UPLOADER
