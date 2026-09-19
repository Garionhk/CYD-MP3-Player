// ===========================================================================
// board.h  --  CYD board profile (pin map + panel facts), selectable at compile
//              time so one code base serves both boards we own.
// ===========================================================================
//
// WHY THIS FILE EXISTS
//   The 2.4" ESP32-2432S024R and the 2.8" ESP32-2432S028R are nearly identical
//   except for the backlight pin, the panel's colour inversion, and -- on the
//   2.8" -- which controller is behind the glass. Everything that differs
//   lives here, once, instead of being sprinkled through sketches.
//
// HOW TO SELECT A BOARD
//   Default is the 2.8" with an ST7789 panel (CYD_BOARD_2432S028R_ST7789). For
//   an older 2.8" with an ILI9341, or the 2.4", define CYD_BOARD before
//   including this file, or edit the #ifndef below.
//
// THE PANEL CONTROLLER IS PART OF THE BOARD PROFILE
//   Boards sold under the one part number ESP32-2432S028R ship with either an
//   ILI9341 or an ST7789 behind identical silkscreen and an identical pin map.
//   The wrong driver never reports an error -- it gives you a blank screen, a
//   white screen, or garbage. So each profile names its controller in
//   CYD_PANEL_DRIVER, the app checks that against the driver TFT_eSPI was
//   actually built with, and the build stops rather than shipping you a dead
//   panel. Each controller has its own User_Setup.h template in config/.
//
//   CYD_PANEL_DRIVER is the DEFAULT, not the last word -- the same arrangement
//   as CYD_TFT_INVERT below. Settings::panel overrides it, and app/panel.cpp
//   initialises the other controller by hand when it does, so one binary drives
//   both panels and the owner of the other one does not need a compiler. What
//   this file decides is what an unprovisioned device boots as.
//
// ARDUINO GOTCHA
//   The IDE copies the sketch folder to a temp dir before building, so a
//   sketch CANNOT #include "../../config/board.h". Each sketch folder carries
//   its own copy. `tools/sync_shared.sh` refreshes those copies from this one
//   -- edit THIS file, then run the script.
//
// SPI BUS LAYOUT (the single most important structural fact)
//   Display : HSPI  -- requires  #define USE_HSPI_PORT  in TFT_eSPI's User_Setup.h
//   SD card : VSPI  -- the MP3 player needs it; pins 18/19/23/5
//   Touch   : BIT-BANGED on its own pins (25/32/39/33)
//   The ESP32 has only two free SPI hosts. The weather clock gave VSPI to touch
//   because it never used the SD slot; the player streams audio from SD, so SD
//   gets the hardware bus and the XPT2046 (polled, <= 2.5 MHz) is clocked by
//   hand. If USE_HSPI_PORT is missing, TFT_eSPI grabs VSPI and the SD card
//   fails to mount. Checked at compile time.
// ===========================================================================

#pragma once

// ---- Panel controllers ----------------------------------------------------
// Which chip is behind the glass. Selected by the board profile below, and
// cross-checked in the app against TFT_eSPI's own *_DRIVER define.
#define CYD_PANEL_ILI9341 9341
#define CYD_PANEL_ST7789  7789

// ---- Board profiles -------------------------------------------------------
#define CYD_BOARD_2432S024R        24   // 2.4", one micro-USB, backlight GPIO27
#define CYD_BOARD_2432S028R        28   // 2.8" ILI9341, micro-USB + USB-C, BL GPIO21
#define CYD_BOARD_2432S028R_ST7789 29   // same 2.8" board, ST7789 panel instead

#ifndef CYD_BOARD
#define CYD_BOARD CYD_BOARD_2432S028R_ST7789
#endif

// ---------------------------------------------------------------------------
// Per-board differences
// ---------------------------------------------------------------------------
#if CYD_BOARD == CYD_BOARD_2432S028R_ST7789

  #define CYD_BOARD_NAME "ESP32-2432S028R (2.8\", micro-USB + USB-C, ST7789)"
  #define CYD_PANEL_DRIVER CYD_PANEL_ST7789   // boot default; Settings::panel wins
  #define CYD_PANEL_NAME   "ST7789"
  // Backlight. Active HIGH through a transistor; the panel is DARK until this
  // is driven, which reads as a dead board if you forget it. Same pin as the
  // ILI9341 build of this board -- only the glass changed.
  #define CYD_TFT_BL_PIN 21
  // Colour inversion -- the BOOT DEFAULT only, not the last word (see the
  // ILI9341 profile below for the whole argument; it applies here unchanged).
  //
  // 1 is right for an ST7789 for a different reason than it is on the ILI9341:
  // these panels are normally-black and want INVON to show anything sane,
  // which is why TFT_eSPI's ST7789 init sequence issues INVON unconditionally.
  // The value still matters, because the app overrides the controller's state
  // with tft.invertDisplay(Settings::invert) right after init -- so 0 here
  // would deliberately undo what the init sequence just did.
  #define CYD_TFT_INVERT 1
  // Landscape rotation. Same mounting as the ILI9341 2.8", so the same value;
  // 3 is the same 320x240 view, 180 deg over.
  #define CYD_ROTATION 1

#elif CYD_BOARD == CYD_BOARD_2432S028R

  #define CYD_BOARD_NAME "ESP32-2432S028R (2.8\", micro-USB + USB-C, ILI9341)"
  #define CYD_PANEL_DRIVER CYD_PANEL_ILI9341
  #define CYD_PANEL_NAME   "ILI9341"
  // Backlight. Active HIGH through a transistor; the panel is DARK until this
  // is driven, which reads as a dead board if you forget it.
  #define CYD_TFT_BL_PIN 21
  // Colour inversion -- the BOOT DEFAULT only, not the last word. CONFIRMED in
  // Stage 0.1 on this unit: without it every colour came back as its exact
  // complement (red->cyan, green->magenta, blue->yellow). Recorded here so the
  // app and the User_Setup.h template can't drift apart -- s01 cross-checks it.
  //
  // Panels sold under this one part number are NOT all wired alike: the same
  // ESP32, the same silkscreen, and a controller that wants the opposite
  // polarity. So this is only what an unprovisioned device comes up with; the
  // stored value in Settings::invert wins from there, and the settings page can
  // flip it without a re-flash. Someone whose board is the other way round can
  // fix it themselves -- which is the whole point, since they cannot rebuild
  // firmware to change a #define.
  #define CYD_TFT_INVERT 1
  // Landscape rotation. CONFIRMED in Stage 0.1: 1 is the right way up on this
  // board (3 is the same 320x240 view, 180 deg over).
  #define CYD_ROTATION 1

#elif CYD_BOARD == CYD_BOARD_2432S024R

  #define CYD_BOARD_NAME "ESP32-2432S024R (2.4\", micro-USB)"
  #define CYD_PANEL_DRIVER CYD_PANEL_ILI9341
  #define CYD_PANEL_NAME   "ILI9341"
  #define CYD_TFT_BL_PIN 27
  #define CYD_TFT_INVERT 1        // boot default; Settings::invert overrides
  #define CYD_ROTATION 3          // that unit read the right way up at 3

#else
  #error "Unknown CYD_BOARD -- set it to CYD_BOARD_2432S028R_ST7789, CYD_BOARD_2432S028R or CYD_BOARD_2432S024R"
#endif

// ---------------------------------------------------------------------------
// Common to both boards
// ---------------------------------------------------------------------------

// ---- TFT (HSPI) -----------------------------------------------------------
// Identical on all three profiles -- ILI9341 and ST7789 sit on the same header
// with the same wiring, which is exactly why the wrong driver is so easy to
// build by accident. Also set in TFT_eSPI's User_Setup.h; duplicated here so
// sketches can check the two agree instead of silently building against a
// stale library config.
#define CYD_TFT_MOSI_PIN 13
#define CYD_TFT_MISO_PIN 12
#define CYD_TFT_SCLK_PIN 14
#define CYD_TFT_CS_PIN   15
#define CYD_TFT_DC_PIN    2
#define CYD_TFT_RST_PIN  -1

// Native panel geometry (portrait). setRotation(1) or (3) -> 320 x 240.
#define CYD_PANEL_W 240
#define CYD_PANEL_H 320

// Landscape geometry. CYD_ROTATION is per-board (above) because the two units
// are mounted the opposite way up; both give the same 320x240 view.
#define CYD_SCREEN_W 320
#define CYD_SCREEN_H 240

// ---- Touch (XPT2046, bit-banged -- VSPI belongs to the SD card) -----------
#define CYD_TOUCH_MOSI_PIN 32
#define CYD_TOUCH_MISO_PIN 39   // input-only pin (GPIO39 = SVN)
#define CYD_TOUCH_CLK_PIN  25
#define CYD_TOUCH_CS_PIN   33
#define CYD_TOUCH_IRQ_PIN  36   // PENIRQ, input-only (GPIO36 = SVP), active LOW

// XPT2046 tops out around 2.5 MHz; going faster returns garbage.
#define CYD_TOUCH_SPI_HZ 2500000

// ---- SD card (hardware VSPI) -- music, backgrounds, system files ----------
#define CYD_SD_MOSI_PIN 23
#define CYD_SD_MISO_PIN 19
#define CYD_SD_SCK_PIN  18
#define CYD_SD_CS_PIN    5

// ---- Other on-board peripherals -------------------------------------------
#define CYD_LDR_PIN     34  // ADC1, light sensor (brighter = lower reading)
#define CYD_RGB_R_PIN    4  // on-board RGB LED, all three ACTIVE LOW
#define CYD_RGB_G_PIN   16
#define CYD_RGB_B_PIN   17
#define CYD_SPEAKER_PIN 26  // DAC2, drives the small speaker pad

// ---------------------------------------------------------------------------
// Small runtime helpers (Arduino only)
// ---------------------------------------------------------------------------
#ifdef ARDUINO

#include <Arduino.h>

// Turn the panel backlight on. Call AFTER tft.init() -- doing it before means
// a bright flash of whatever garbage the controller powered up with.
inline void cydBacklightOn() {
  pinMode(CYD_TFT_BL_PIN, OUTPUT);
  digitalWrite(CYD_TFT_BL_PIN, HIGH);
}

inline void cydBacklightOff() {
  pinMode(CYD_TFT_BL_PIN, OUTPUT);
  digitalWrite(CYD_TFT_BL_PIN, LOW);
}

// Park the on-board RGB LED. It is active LOW, so it powers up glowing white
// and washes out the panel's colours if left alone.
inline void cydRgbLedOff() {
  pinMode(CYD_RGB_R_PIN, OUTPUT);
  pinMode(CYD_RGB_G_PIN, OUTPUT);
  pinMode(CYD_RGB_B_PIN, OUTPUT);
  digitalWrite(CYD_RGB_R_PIN, HIGH);
  digitalWrite(CYD_RGB_G_PIN, HIGH);
  digitalWrite(CYD_RGB_B_PIN, HIGH);
}

// Identification banner, printed at the top of every sketch so
// a serial log always says which board it came from.
inline void cydPrintBanner() {
  Serial.println();
  Serial.printf("board   : %s\n", CYD_BOARD_NAME);
  Serial.printf("chip    : %s rev %d, %d core(s) @ %d MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), getCpuFrequencyMhz());
  Serial.printf("flash   : %u bytes   free heap: %u\n",
                ESP.getFlashChipSize(), ESP.getFreeHeap());
  Serial.printf("panel   : %s, invert default %d, rotation %d\n",
                CYD_PANEL_NAME, CYD_TFT_INVERT, CYD_ROTATION);
  Serial.printf("tft     : MOSI%d MISO%d SCLK%d CS%d DC%d RST%d BL%d (HSPI)\n",
                CYD_TFT_MOSI_PIN, CYD_TFT_MISO_PIN, CYD_TFT_SCLK_PIN,
                CYD_TFT_CS_PIN, CYD_TFT_DC_PIN, CYD_TFT_RST_PIN, CYD_TFT_BL_PIN);
  Serial.printf("touch   : MOSI%d MISO%d CLK%d CS%d IRQ%d (bit-bang)\n",
                CYD_TOUCH_MOSI_PIN, CYD_TOUCH_MISO_PIN, CYD_TOUCH_CLK_PIN,
                CYD_TOUCH_CS_PIN, CYD_TOUCH_IRQ_PIN);
  Serial.printf("sd      : MOSI%d MISO%d SCK%d CS%d (VSPI)\n",
                CYD_SD_MOSI_PIN, CYD_SD_MISO_PIN, CYD_SD_SCK_PIN, CYD_SD_CS_PIN);
}

#endif  // ARDUINO
