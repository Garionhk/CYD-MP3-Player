#include "display.h"
#include "settings.h"
#include "touch.h"

TFT_eSPI tft = TFT_eSPI();

// ---------------------------------------------------------------------------
// The two initialisation sequences, vendored from TFT_eSPI
// ---------------------------------------------------------------------------
// Copied from the weather clock's panel.cpp, which transcribed them from
// TFT_Drivers/ILI9341_Init.h (the ILI9341_2 alternative branch) and
// TFT_Drivers/ST7789_Init.h. If a TFT_eSPI update changes one of those and the
// patched panel misbehaves, diff these tables against the library first.
//
// Format: cmd, len, args...   0x80 in len = a delay in ms follows the args.
static const uint8_t TBL_END   = 0xFF;
static const uint8_t TBL_DELAY = 0x80;

static const uint8_t ILI9341_TABLE[] PROGMEM = {
  0xCF, 3, 0x00, 0xC1, 0x30,
  0xED, 4, 0x64, 0x03, 0x12, 0x81,
  0xE8, 3, 0x85, 0x00, 0x78,
  0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
  0xF7, 1, 0x20,
  0xEA, 2, 0x00, 0x00,
  0xC0, 1, 0x10,
  0xC1, 1, 0x00,
  0xC5, 2, 0x30, 0x30,
  0xC7, 1, 0xB7,
  0x3A, 1, 0x55,
  0x36, 1, 0x08,
  0xB1, 2, 0x00, 0x1A,
  0xB6, 3, 0x08, 0x82, 0x27,
  0xF2, 1, 0x00,
  0x26, 1, 0x01,
  0xE0, 15, 0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9,
            0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00,
  0xE1, 15, 0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56,
            0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F,
  0x2B, 4, 0x00, 0x00, 0x01, 0x3F,
  0x2A, 4, 0x00, 0x00, 0x00, 0xEF,
  0x11, 0 | TBL_DELAY, 120,
  0x29, 0,
  TBL_END
};

static const uint8_t ST7789_TABLE[] PROGMEM = {
  0x11, 0 | TBL_DELAY, 120,
  0x13, 0,
  0x36, 1, TFT_MAD_COLOR_ORDER,
  0xB6, 2, 0x0A, 0x82,
  0xB0, 2, 0x00, 0xE0,
  0x3A, 1 | TBL_DELAY, 0x55, 10,
  0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
  0xB7, 1, 0x35,
  0xBB, 1, 0x28,
  0xC0, 1, 0x0C,
  0xC2, 2, 0x01, 0xFF,
  0xC3, 1, 0x10,
  0xC4, 1, 0x20,
  0xC6, 1, 0x0F,
  0xD0, 2, 0xA4, 0xA1,
  0xE0, 14, 0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32,
            0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17,
  0xE1, 14, 0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31,
            0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E,
  0x21, 0,
  0x2A, 4, 0x00, 0x00, 0x00, 0xEF,
  0x2B, 4 | TBL_DELAY, 0x00, 0x00, 0x01, 0x3F, 120,
  0x29, 0 | TBL_DELAY, 120,
  TBL_END
};

static bool patched = false;

static void sendTable(const uint8_t* t) {
  for (;;) {
    const uint8_t cmd = pgm_read_byte(t++);
    if (cmd == TBL_END) return;
    const uint8_t len = pgm_read_byte(t++);
    tft.writecommand(cmd);
    for (uint8_t i = 0; i < (len & 0x7F); i++) tft.writedata(pgm_read_byte(t++));
    if (len & TBL_DELAY) delay(pgm_read_byte(t++));
  }
}

// MADCTL per controller per rotation, from TFT_eSPI's *_Rotation.h.
static uint8_t madctlFor(uint16_t panelId, uint8_t rot) {
  rot &= 3;
  if (panelId == CYD_PANEL_ST7789) {
    static const uint8_t m[4] = {
      TFT_MAD_COLOR_ORDER,
      TFT_MAD_MX | TFT_MAD_MV | TFT_MAD_COLOR_ORDER,
      TFT_MAD_MX | TFT_MAD_MY | TFT_MAD_COLOR_ORDER,
      TFT_MAD_MV | TFT_MAD_MY | TFT_MAD_COLOR_ORDER,
    };
    return m[rot];
  }
  static const uint8_t m[4] = {
    TFT_MAD_MX | TFT_MAD_COLOR_ORDER,
    TFT_MAD_MV | TFT_MAD_COLOR_ORDER,
    TFT_MAD_MY | TFT_MAD_COLOR_ORDER,
    TFT_MAD_MX | TFT_MAD_MY | TFT_MAD_MV | TFT_MAD_COLOR_ORDER,
  };
  return m[rot];
}

const char* display_panelName(uint16_t panelId) {
  return panelId == CYD_PANEL_ST7789 ? "ST7789" : "ILI9341";
}

void display_setRotation(uint8_t rot) {
  tft.setRotation(rot & 3);
  if (!patched) return;
  tft.writecommand(TFT_MADCTL);
  tft.writedata(madctlFor(g_settings.panel, rot));
}

// 5 kHz is above anything visible as flicker and below where the backlight
// transistor starts rounding the edges off.
static const uint32_t BL_PWM_HZ   = 5000;
static const uint8_t  BL_PWM_BITS = 8;
static bool blAttached = false;

void display_setBacklight(uint8_t percent) {
  if (!blAttached) {
    ledcAttach(CYD_TFT_BL_PIN, BL_PWM_HZ, BL_PWM_BITS);
    blAttached = true;
  }
  percent = constrain(percent, 0, 100);
  ledcWrite(CYD_TFT_BL_PIN, (uint32_t)percent * 255 / 100);
}

void display_begin() {
  // Backlight dark first: tft.init() leaves the controller showing whatever it
  // powered up with for a moment.
  pinMode(CYD_TFT_BL_PIN, OUTPUT);
  digitalWrite(CYD_TFT_BL_PIN, LOW);

  tft.init();
  patched = (g_settings.panel != CYD_PANEL_DRIVER);
  if (patched) {
    tft.writecommand(TFT_SWRST);
    delay(150);
    sendTable(g_settings.panel == CYD_PANEL_ST7789 ? ST7789_TABLE : ILI9341_TABLE);
  }
  display_setRotation(g_settings.rotation);
  tft.invertDisplay(g_settings.invert);

  Serial.printf("panel   : %s%s  rotation %u  invert %s\n",
                display_panelName(g_settings.panel),
                patched ? " (runtime patch over a " CYD_PANEL_NAME " build)" : " (compiled in)",
                g_settings.rotation, g_settings.invert ? "on" : "off");
}

// ---------------------------------------------------------------------------
// Boot gesture -- as the weather clock, minus its "force setup portal" branch
// ---------------------------------------------------------------------------
static const uint32_t SWITCH_WARN_MS = 4000;
static const uint32_t SWITCH_HOLD_MS = 8000;
static const uint32_t ARRIVE_WINDOW_MS = 3000;

static void blueLed(bool on) {
  pinMode(CYD_RGB_B_PIN, OUTPUT);
  digitalWrite(CYD_RGB_B_PIN, on ? LOW : HIGH);   // active LOW
}

void display_bootGesture() {
  int rx, ry, z;
  // A board that has never been calibrated may be one whose screen shows
  // nothing -- the case this gesture exists for -- and its owner cannot see
  // when to press. So on that boot, wait a few seconds for a finger with the
  // blue LED lit ("press now"). Every later boot takes one sample and moves on.
  const uint32_t window = touch_hasStoredCalibration() ? 0 : ARRIVE_WINDOW_MS;
  const uint32_t opened = millis();
  blueLed(window > 0);
  while (!touch_readRaw(rx, ry, z)) {
    if (millis() - opened >= window) { blueLed(false); return; }
    delay(25);
  }
  blueLed(false);

  const uint32_t start = millis();
  uint8_t misses = 0;
  while (millis() - start < SWITCH_HOLD_MS) {
    // A resistive panel drops the odd sample under a steady finger; four
    // misses in a row (100 ms) is a release.
    if (touch_readRaw(rx, ry, z)) misses = 0;
    else if (++misses >= 4) { blueLed(false); return; }
    const uint32_t held = millis() - start;
    if (held >= SWITCH_WARN_MS) blueLed((held / 250) & 1);
    delay(25);
  }

  const uint16_t next = (g_settings.panel == CYD_PANEL_ST7789) ? CYD_PANEL_ILI9341
                                                              : CYD_PANEL_ST7789;
  Serial.printf("panel   : boot gesture -- switching %s -> %s, restarting\n",
                display_panelName(g_settings.panel), display_panelName(next));
  g_settings.panel = next;
  settings_savePanel();
  for (int i = 0; i < 3; i++) { blueLed(true); delay(120); blueLed(false); delay(120); }
  delay(200);
  ESP.restart();
}
