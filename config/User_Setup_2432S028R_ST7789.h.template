// ===========================================================================
// User_Setup.h  --  TFT_eSPI configuration for ESP32-2432S028R (CYD 2.8")
//                   with an ST7789 panel controller
// ===========================================================================
//
// WHY THERE ARE TWO 2.8" TEMPLATES
//   Boards sold as ESP32-2432S028R ship with one of two panel controllers.
//   Same ESP32, same silkscreen, same pin map, different glass: earlier units
//   are ILI9341, later ones are ST7789. Nothing on the outside of the board
//   says which you have, and the wrong driver gives you a blank screen, a
//   white screen, or a smear of garbage -- never an error message.
//   User_Setup_2432S028R.h.template is the ILI9341 file; this is the ST7789
//   one. Everything except the driver block below is identical between them.
//
// HOW TO USE
//   1. Locate the TFT_eSPI library folder:
//        ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
//   2. Replace that library's User_Setup.h with this file's contents.
//        cp config/User_Setup_2432S028R_ST7789.h.template \
//           ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
//   3. Set CYD_BOARD to CYD_BOARD_2432S028R_ST7789 in config/board.h (it is
//      the default) and run tools/sync_shared.sh. The app cross-checks this
//      file against board.h at COMPILE time and refuses to build if the two
//      disagree -- that catches "I forgot to swap User_Setup.h", which is the
//      single most common way this bring-up goes wrong.
//   4. The moment the display is right, copy your final User_Setup.h back over
//      this template so a library update can't silently wipe your settings.
// ===========================================================================

// ---- Driver ---------------------------------------------------------------
// ST7789_DRIVER is TFT_eSPI's standard ST7789 init sequence. If the screen
// stays blank or shows garbage, the next thing to try is ST7789_2_DRIVER (the
// alternative init sequence -- some panel lots want it); after that you are
// probably holding an ILI9341 board, so use the other template.
//
// Do NOT add CGRAM_OFFSET. It exists for the small ST7789 panels (135x240,
// 240x240) whose visible area sits inside a larger controller memory; this one
// is a full 240x320 and defining it would push the whole image off-centre.
#define ST7789_DRIVER

// ---- Panel geometry (portrait native; setRotation(1|3) -> 320x240) --------
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ---- Colour handling ------------------------------------------------------
// Symptom -> fix:
//   every colour is its exact complement (red->cyan, green->magenta,
//   blue->yellow)          -> flip inversion (see below)
//   only red and blue are swapped, green fine
//                          -> switch TFT_RGB_ORDER between TFT_BGR and TFT_RGB
//
// INVERSION ON ST7789 IS NOT LIKE ILI9341. TFT_eSPI's ST7789 init sequence
// (TFT_Drivers/ST7789_Init.h) issues INVON unconditionally, so an ST7789 panel
// comes up inverted whatever you write here -- the default is the opposite way
// round from the ILI9341 file, where "no #define" means "not inverted".
// TFT_INVERSION_ON below is therefore redundant at the controller; it is kept
// so this file states the panel's polarity out loud and stays comparable with
// the ILI9341 template, which is what CYD_TFT_INVERT in board.h is checked
// against.
//
// None of this is the last word anyway: the app calls tft.invertDisplay() with
// the stored Settings::invert immediately after tft.init(), so the settings
// page can flip a panel that came out complemented without a re-flash. What
// this file sets is only what an unprovisioned device boots with.
#define TFT_INVERSION_ON
//#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_BGR

// ---- SPI pins -- must match config/board.h ---------------------------------
// Same header, same wiring as the ILI9341 board. MISO is listed for pin-map
// parity only: this panel is never read back (ST7789 SPI reads are unreliable
// at best), and nothing in the app calls readPixel.
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
// -1 = no reset pin wired out; the LCD reset sits on the ESP32's EN line, so
// TFT_eSPI issues a software reset (SWRESET) at init instead.
#define TFT_RST  -1

// ---- SPI port -- REQUIRED, and easy to miss --------------------------------
// Without this, TFT_eSPI puts the display on VSPI, which is the bus the touch
// controller needs. The display still works, so the mistake shows up later as
// a touch panel that reads a constant 0. Keep the display on HSPI.
#define USE_HSPI_PORT

// ---- Backlight -------------------------------------------------------------
// Deliberately NOT configured here. The backlight pin differs between our
// boards, so it is driven from code via cydBacklightOn() in config/board.h --
// one source of truth, and it leaves the door open for PWM dimming off the LDR
// later. Do not add TFT_BL / TFT_BACKLIGHT_ON. (The ST7789 init sequence would
// also drive TFT_BL itself if it were defined, which would light the panel
// before anything has been drawn on it -- a flash of power-up garbage.)

// ---- Fonts ----------------------------------------------------------------
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// ---- SPI speed ------------------------------------------------------------
// 40 MHz is the conservative bring-up speed for an unvalidated ST7789 lot; the
// ILI9341 board runs 55 MHz (measured, docs/stage0_results.md). Once the panel
// is stable, work up: 55000000, then 65000000. Faint, flickering, tearing or
// intermittently corrupt output means you went too far -- come back down.
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

// TFT_eSPI's own touch support is unused -- the XPT2046 is driven by
// XPT2046_Touchscreen on its own VSPI bus. Left defined only because some
// TFT_eSPI builds reference it.
#define SPI_TOUCH_FREQUENCY  2500000
