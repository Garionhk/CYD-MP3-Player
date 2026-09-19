// ===========================================================================
// Stage 0.1 -- display + bit-banged touch + SD card, all at once
// ===========================================================================
// The one structural risk in the MP3 player's wiring (plan §1): the weather
// clock gave VSPI to the touch panel, and the player needs VSPI for the SD
// card. This sketch proves the new split works on the real board before any
// player code depends on it:
//
//   Display : HSPI (TFT_eSPI, USE_HSPI_PORT)
//   SD      : VSPI (hardware, pins 18/19/23/5)
//   Touch   : bit-banged XPT2046 (pins 25/32/39/33)
//
// What to look for:
//   1. The panel draws (the header bar and the SD panel).
//   2. SD mounts, the size is sane, /music /bg /.sys exist afterwards.
//   3. The read-speed line: an MP3 at 320 kbps needs 40 KB/s. Anything in the
//      hundreds is plenty of headroom.
//   4. Touching the glass prints raw x/y/z on screen and serial, and draws a
//      dot roughly under your finger (uncalibrated -- tens of px out is fine).
//   5. TOUCHING WHILE THE SD TEST RUNS still works -- tap "SD test" and keep
//      pressing. That is the bus-contention case this sketch exists for.
//   6. Long-press (> 1 s) cycles the rotation 0..3, so portrait and landscape
//      both get looked at.
// ===========================================================================

#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include "board.h"

#ifndef USE_HSPI_PORT
  #error "User_Setup.h is missing '#define USE_HSPI_PORT' -- the display would take VSPI and the SD card could not mount. Copy config/User_Setup_2432S028R_ST7789.h.template into the TFT_eSPI library."
#endif

TFT_eSPI tft;
SPIClass sdSPI(VSPI);

// ---------------------------------------------------------------------------
// Bit-banged XPT2046
// ---------------------------------------------------------------------------
// Control byte: S A2 A1 A0 MODE SER/DFR PD1 PD0. 12-bit, differential.
// PD = 01 keeps the ADC on between samples of one reading; the final Y read
// uses PD = 00 so the chip powers down and PENIRQ is armed again.
static const uint8_t XPT_X  = 0xD1;
static const uint8_t XPT_Y  = 0x91;
static const uint8_t XPT_Z1 = 0xB1;
static const uint8_t XPT_Z2 = 0xC1;
static const uint8_t XPT_Y_PD = 0x90;

// ~100 kHz clock. The chip allows 2.5 MHz; slower costs nothing when polled
// at loop rate and is far more tolerant of long traces.
static inline void xptClockDelay() { delayMicroseconds(5); }

static uint16_t xptTransfer(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(CYD_TOUCH_MOSI_PIN, (cmd >> i) & 1);
    digitalWrite(CYD_TOUCH_CLK_PIN, HIGH); xptClockDelay();
    digitalWrite(CYD_TOUCH_CLK_PIN, LOW);  xptClockDelay();
  }
  digitalWrite(CYD_TOUCH_MOSI_PIN, LOW);
  // The chip shifts a data bit out on each falling edge after the control
  // byte (null bit first, then MSB). Reading after 12 more falling edges
  // collects D11..D0.
  uint16_t v = 0;
  for (int i = 11; i >= 0; i--) {
    digitalWrite(CYD_TOUCH_CLK_PIN, HIGH); xptClockDelay();
    digitalWrite(CYD_TOUCH_CLK_PIN, LOW);  xptClockDelay();
    v |= (uint16_t)digitalRead(CYD_TOUCH_MISO_PIN) << i;
  }
  return v;
}

static void xptBegin() {
  pinMode(CYD_TOUCH_CS_PIN, OUTPUT);
  pinMode(CYD_TOUCH_CLK_PIN, OUTPUT);
  pinMode(CYD_TOUCH_MOSI_PIN, OUTPUT);
  pinMode(CYD_TOUCH_MISO_PIN, INPUT);
  pinMode(CYD_TOUCH_IRQ_PIN, INPUT);
  digitalWrite(CYD_TOUCH_CS_PIN, HIGH);
  digitalWrite(CYD_TOUCH_CLK_PIN, LOW);
}

// Same z convention as XPT2046_Touchscreen (z1 + 4095 - z2), so the weather
// clock's press threshold of ~400 means the same thing here.
static void xptRead(int& x, int& y, int& z) {
  digitalWrite(CYD_TOUCH_CS_PIN, LOW);
  const int z1 = xptTransfer(XPT_Z1);
  const int z2 = xptTransfer(XPT_Z2);
  z = z1 + 4095 - z2;
  if (z < 0) z = 0;

  // Three samples each; the first after switching channel is the noisiest,
  // so it is thrown away and the other two averaged.
  xptTransfer(XPT_X);
  const int x1 = xptTransfer(XPT_X), x2 = xptTransfer(XPT_X);
  xptTransfer(XPT_Y);
  const int y1 = xptTransfer(XPT_Y), y2 = xptTransfer(XPT_Y_PD);
  digitalWrite(CYD_TOUCH_CS_PIN, HIGH);

  x = (x1 + x2) / 2;
  y = (y1 + y2) / 2;
}

// Mapping from raw to the native PORTRAIT panel (240x320), then rotated to
// whatever rotation is active.
//
// Derived from the calibration the weather clock stored on THIS board
// (x[193..3782] y[3919..199] swap=1 at rotation 1), translated through two
// differences from that firmware:
//   - XPT2046_Touchscreen pipelines its SPI reads, so its "x" is the 0x9x
//     channel and its "y" the 0xDx channel -- the reverse of the names here;
//   - at its setRotation(0) it reports xraw = 4095 - y.
// Net result: raw X (0xD1) runs along the 240 edge and DECREASES left to right
// in portrait. The first version of this sketch had it increasing, which put
// the dot on the mirror-image side of the screen.
static void rawToScreen(int rx, int ry, int& sx, int& sy) {
  int px = constrain(map(rx, 3896, 176, 0, CYD_PANEL_W - 1), 0, CYD_PANEL_W - 1);
  int py = constrain(map(ry, 193, 3782, 0, CYD_PANEL_H - 1), 0, CYD_PANEL_H - 1);
  switch (tft.getRotation()) {
    case 0: sx = px;                   sy = py;                   break;
    case 1: sx = py;                   sy = CYD_PANEL_W - 1 - px; break;
    case 2: sx = CYD_PANEL_W - 1 - px; sy = CYD_PANEL_H - 1 - py; break;
    default:sx = CYD_PANEL_H - 1 - py; sy = px;                   break;
  }
}

// ---------------------------------------------------------------------------
// SD
// ---------------------------------------------------------------------------
static bool     sdOk = false;
static String   sdLine1, sdLine2, sdLine3, sdLine4;

static int countFiles(const char* dir, const char* ext) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return -1;
  int n = 0;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory()) {
      String name = f.name();
      name.toLowerCase();
      if (name.endsWith(ext)) n++;
    }
    f.close();
  }
  d.close();
  return n;
}

static void sdMount() {
  sdSPI.begin(CYD_SD_SCK_PIN, CYD_SD_MISO_PIN, CYD_SD_MOSI_PIN, CYD_SD_CS_PIN);
  // 20 MHz: well above what 320 kbps audio needs, and cards that refuse it
  // are retried at 4 MHz rather than reported as missing.
  sdOk = SD.begin(CYD_SD_CS_PIN, sdSPI, 20000000);
  uint32_t hz = 20000000;
  if (!sdOk) {
    SD.end();
    hz = 4000000;
    sdOk = SD.begin(CYD_SD_CS_PIN, sdSPI, hz);
  }
  if (!sdOk) {
    sdLine1 = "SD: mount FAILED";
    sdLine2 = "card inserted? FAT32?";
    Serial.println("sd: mount failed at 20 MHz and 4 MHz");
    return;
  }

  static const char* TYPES[] = { "none", "MMC", "SD", "SDHC", "unknown" };
  const uint8_t t = SD.cardType();
  sdLine1 = String("SD ") + TYPES[t < 4 ? t : 4] + "  " +
            String((uint32_t)(SD.cardSize() / (1024ULL * 1024ULL))) + " MB  @" +
            String(hz / 1000000) + " MHz";

  for (const char* dir : { "/music", "/bg", "/.sys" }) {
    if (!SD.exists(dir)) {
      Serial.printf("sd: creating %s -> %s\n", dir, SD.mkdir(dir) ? "ok" : "FAILED");
    }
  }
  sdLine2 = "music/*.mp3: " + String(countFiles("/music", ".mp3")) +
            "   bg/*.gif: " + String(countFiles("/bg", ".gif"));
  sdLine3 = "tap 'SD test' to read-speed test";
  Serial.println("sd: " + sdLine1);
  Serial.println("sd: " + sdLine2);
}

// Read the first file found in /music (or any file at all) for ~2 s while
// touch keeps being polled, and report throughput.
static void sdSpeedTest() {
  if (!sdOk) return;
  File d = SD.open("/music");
  File f;
  if (d) {
    for (File c = d.openNextFile(); c; c = d.openNextFile()) {
      if (!c.isDirectory() && c.size() > 256 * 1024) { f = c; break; }
      c.close();
    }
    d.close();
  }
  if (!f) {
    sdLine3 = "no file > 256 KB in /music to test";
    return;
  }
  static uint8_t buf[4096];
  uint32_t bytes = 0, touchesDuring = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < 2000) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) { f.seek(0); continue; }
    bytes += n;
    int rx, ry, rz;
    xptRead(rx, ry, rz);             // interleave touch with SD traffic
    if (rz > 400) touchesDuring++;
  }
  const uint32_t ms = millis() - t0;
  f.close();
  sdLine3 = "read " + String(bytes / 1024 * 1000 / ms) + " KB/s (need 40)";
  sdLine4 = "touch samples during test: " + String(touchesDuring);
  Serial.println("sd: " + sdLine3 + "  " + sdLine4);
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------
static const int BTN_W = 90, BTN_H = 36;

static void drawAll() {
  const int W = tft.width(), H = tft.height();
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, W, 22, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("s01  rot " + String(tft.getRotation()) + "  " +
                 String(W) + "x" + String(H), 6, 11, 2);

  tft.setTextColor(sdOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.drawString(sdLine1, 6, 36, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(sdLine2, 6, 54, 2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(sdLine3, 6, 72, 2);
  tft.drawString(sdLine4, 6, 90, 2);

  // "SD test" button, bottom-right in every rotation.
  tft.fillRoundRect(W - BTN_W - 6, H - BTN_H - 6, BTN_W, BTN_H, 6, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SD test", W - BTN_W / 2 - 6, H - BTN_H / 2 - 6, 2);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("long-press: rotate", 6, H - 24, 2);
}

static void drawRaw(int rx, int ry, int rz) {
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  char buf[48];
  snprintf(buf, sizeof(buf), "raw x%4d y%4d z%4d   ", rx, ry, rz);
  tft.drawString(buf, 6, 112, 2);
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== s01 display + bit-bang touch + SD ===");
  cydPrintBanner();

  cydRgbLedOff();
  tft.init();
  tft.setRotation(CYD_ROTATION);
  tft.invertDisplay(CYD_TFT_INVERT);
  cydBacklightOn();

  xptBegin();
  sdMount();
  drawAll();
  Serial.printf("heap free %u\n", ESP.getFreeHeap());
}

static bool     down = false;
static uint32_t downAt = 0, lastSeen = 0;
static int      lastSx = 0, lastSy = 0;

void loop() {
  int rx, ry, rz;
  xptRead(rx, ry, rz);
  const uint32_t now = millis();

  if (rz > 400) {
    if (!down) { down = true; downAt = now; }
    lastSeen = now;
    rawToScreen(rx, ry, lastSx, lastSy);
    tft.fillCircle(lastSx, lastSy, 2, TFT_MAGENTA);
    drawRaw(rx, ry, rz);
    static uint32_t lastLog = 0;
    if (now - lastLog > 200) {
      lastLog = now;
      Serial.printf("touch raw x%d y%d z%d -> screen %d,%d\n", rx, ry, rz, lastSx, lastSy);
    }
  } else if (down && now - lastSeen > 60) {
    down = false;
    const uint32_t held = lastSeen - downAt;
    const int W = tft.width(), H = tft.height();
    if (held > 1000) {
      tft.setRotation((tft.getRotation() + 1) % 4);
      Serial.printf("rotation -> %d (%dx%d)\n", tft.getRotation(), tft.width(), tft.height());
      drawAll();
    } else if (lastSx > W - BTN_W - 30 && lastSy > H - BTN_H - 30) {
      sdLine3 = "testing...";
      drawAll();
      sdSpeedTest();
      drawAll();
    }
  }
  delay(10);
}
