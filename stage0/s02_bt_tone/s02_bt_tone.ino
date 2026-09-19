// ===========================================================================
// Stage 0.2 -- Bluetooth A2DP source: scan, pick a speaker on screen, play tone
// ===========================================================================
// Proves the Bluetooth half of the player on its own, before MP3 decoding is
// stacked on top (plan §6):
//
//   1. Put your Bluetooth speaker / headphones in PAIRING mode.
//   2. Found devices appear as a list with signal strength (refreshes as the
//      ESP32 keeps scanning -- a device can take 10-20 s to show up).
//   3. Tap one. The ESP32 connects on its next sighting of that device.
//   4. Once connected you hear a 440 Hz tone. Buttons: Tone on/off, Vol-, Vol+.
//
// Serial logs connection state and, every 5 s, free heap and how many audio
// frames the Bluetooth stack pulled -- ~44100 per second means the stream is
// healthy.
//
// This is also the prototype of the player's Bluetooth screen: the SSID
// callback never auto-connects, it only fills the list, and returns true for
// the one device the user tapped.
// ===========================================================================

#include <TFT_eSPI.h>
#include "BluetoothA2DPSource.h"
#include "board.h"
#include "btn.h"

#ifndef USE_HSPI_PORT
  #error "User_Setup.h is missing '#define USE_HSPI_PORT'. Copy config/User_Setup_2432S028R_ST7789.h.template into the TFT_eSPI library."
#endif

TFT_eSPI tft;
BluetoothA2DPSource a2dp;

// ---------------------------------------------------------------------------
// Bit-banged XPT2046 -- same as s01 (verified there, mapping included)
// ---------------------------------------------------------------------------
static inline void xptClockDelay() { delayMicroseconds(5); }

static uint16_t xptTransfer(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(CYD_TOUCH_MOSI_PIN, (cmd >> i) & 1);
    digitalWrite(CYD_TOUCH_CLK_PIN, HIGH); xptClockDelay();
    digitalWrite(CYD_TOUCH_CLK_PIN, LOW);  xptClockDelay();
  }
  digitalWrite(CYD_TOUCH_MOSI_PIN, LOW);
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
  digitalWrite(CYD_TOUCH_CS_PIN, HIGH);
  digitalWrite(CYD_TOUCH_CLK_PIN, LOW);
}

static void xptRead(int& x, int& y, int& z) {
  digitalWrite(CYD_TOUCH_CS_PIN, LOW);
  const int z1 = xptTransfer(0xB1);
  const int z2 = xptTransfer(0xC1);
  z = max(0, z1 + 4095 - z2);
  xptTransfer(0xD1);
  const int x1 = xptTransfer(0xD1), x2 = xptTransfer(0xD1);
  xptTransfer(0x91);
  const int y1 = xptTransfer(0x91), y2 = xptTransfer(0x90);
  digitalWrite(CYD_TOUCH_CS_PIN, HIGH);
  x = (x1 + x2) / 2;
  y = (y1 + y2) / 2;
}

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
// Device list, filled from the Bluetooth task, drawn from loop()
// ---------------------------------------------------------------------------
struct BtDev {
  char     name[33];
  uint8_t  addr[6];
  int      rssi;
  uint32_t seenMs;
};
static const int MAX_DEVS = 6;
static BtDev     devs[MAX_DEVS];
static int       devCount = 0;
static int       chosen = -1;               // index the user tapped
static bool      listDirty = true;
static portMUX_TYPE devMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool     connected = false;
static volatile bool     stateDirty = true;
static volatile bool     toneOn = true;
static volatile int      volumePct = 30;
static volatile uint32_t framesServed = 0;

// Runs on the Bluetooth task for every device discovery reports.
static bool onSsid(const char* ssid, esp_bd_addr_t address, int rssi) {
  bool connect = false;
  portENTER_CRITICAL(&devMux);
  int i = 0;
  for (; i < devCount; i++)
    if (memcmp(devs[i].addr, address, 6) == 0) break;
  if (i == devCount && devCount < MAX_DEVS) devCount++;
  if (i < devCount) {
    strlcpy(devs[i].name, (ssid && *ssid) ? ssid : "(no name)", sizeof(devs[i].name));
    memcpy(devs[i].addr, address, 6);
    devs[i].rssi   = rssi;
    devs[i].seenMs = millis();
    connect = (i == chosen);
    listDirty = true;
  }
  portEXIT_CRITICAL(&devMux);
  Serial.printf("bt: found \"%s\" %02x:%02x:%02x:%02x:%02x:%02x rssi %d%s\n",
                ssid, address[0], address[1], address[2], address[3], address[4],
                address[5], rssi, connect ? "  -> CONNECT" : "");
  return connect;
}

static void onConnState(esp_a2d_connection_state_t state, void*) {
  static const char* NAMES[] = { "disconnected", "connecting", "connected", "disconnecting" };
  Serial.printf("bt: state %s\n", state <= 3 ? NAMES[state] : "?");
  connected  = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
  stateDirty = true;
}

// 44.1 kHz stereo 16-bit, pulled by the Bluetooth stack. Must be quick: no
// logging, no allocation.
static int32_t getFrames(Frame* frame, int32_t count) {
  static float phase = 0;
  const float step = 2.0f * PI * 440.0f / 44100.0f;
  const float amp  = toneOn ? 12000.0f * volumePct / 100.0f : 0.0f;
  for (int i = 0; i < count; i++) {
    const int16_t s = (int16_t)(amp * sin(phase));
    frame[i].channel1 = s;
    frame[i].channel2 = s;
    phase += step;
    if (phase > 2.0f * PI) phase -= 2.0f * PI;
  }
  framesServed += count;
  return count;
}

// ---------------------------------------------------------------------------
// Screen (landscape 320x240)
// ---------------------------------------------------------------------------
static const int ROW_Y0 = 30, ROW_H = 26;
static const Btn BTN_TONE = {   6, 200, 100, 34, "Tone" };
static const Btn BTN_VDN  = { 112, 200, 100, 34, "Vol -" };
static const Btn BTN_VUP  = { 218, 200,  96, 34, "Vol +" };

static bool hit(const Btn& b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static void drawBtn(const Btn& b, uint16_t bg, const String& label) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2, 2);
}

static void drawHeader() {
  tft.fillRect(0, 0, tft.width(), 22, connected ? TFT_DARKGREEN : TFT_NAVY);
  tft.setTextColor(TFT_WHITE, connected ? TFT_DARKGREEN : TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  String s = connected ? "s02  CONNECTED - playing tone"
                       : (chosen >= 0 ? "s02  connecting..." : "s02  tap your speaker");
  tft.drawString(s, 6, 11, 2);
}

static void drawButtons() {
  drawBtn(BTN_TONE, toneOn ? TFT_DARKGREEN : TFT_DARKGREY, toneOn ? "Tone ON" : "Tone OFF");
  drawBtn(BTN_VDN, TFT_DARKGREY, "Vol -");
  drawBtn(BTN_VUP, TFT_DARKGREY, "Vol +  " + String(volumePct) + "%");
}

static void drawList() {
  BtDev copy[MAX_DEVS];
  int n, sel;
  portENTER_CRITICAL(&devMux);
  n = devCount; sel = chosen;
  memcpy(copy, devs, sizeof(copy));
  listDirty = false;
  portEXIT_CRITICAL(&devMux);

  tft.fillRect(0, ROW_Y0, tft.width(), ROW_H * MAX_DEVS, TFT_BLACK);
  if (n == 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("scanning... put the speaker in pairing mode", 6, ROW_Y0 + 12, 2);
    return;
  }
  for (int i = 0; i < n; i++) {
    const int y = ROW_Y0 + i * ROW_H;
    const uint16_t bg = (i == sel) ? TFT_MAROON : 0x2104;
    tft.fillRoundRect(4, y + 1, tft.width() - 8, ROW_H - 3, 4, bg);
    tft.setTextColor(TFT_WHITE, bg);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(copy[i].name, 10, y + ROW_H / 2, 2);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(String(copy[i].rssi) + " dBm", tft.width() - 10, y + ROW_H / 2, 2);
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== s02 Bluetooth A2DP tone ===");
  cydPrintBanner();

  cydRgbLedOff();
  tft.init();
  tft.setRotation(CYD_ROTATION);
  tft.invertDisplay(CYD_TFT_INVERT);
  tft.fillScreen(TFT_BLACK);
  cydBacklightOn();
  xptBegin();

  Serial.printf("heap before bt: %u\n", ESP.getFreeHeap());
  a2dp.set_local_name("CYD-MP3");
  a2dp.set_ssid_callback(onSsid);
  a2dp.set_on_connection_state_changed(onConnState);
  a2dp.set_data_callback_in_frames(getFrames);
  a2dp.set_auto_reconnect(false);      // this test always picks from the list
  a2dp.set_volume(100);                // gain is applied in getFrames instead
  a2dp.start();
  Serial.printf("heap after bt start: %u\n", ESP.getFreeHeap());

  drawHeader();
  drawList();
  drawButtons();
}

static bool     down = false;
static uint32_t lastSeen = 0;
static int      lastSx = 0, lastSy = 0;

void loop() {
  const uint32_t now = millis();

  int rx, ry, rz;
  xptRead(rx, ry, rz);
  if (rz > 400) {
    down = true;
    lastSeen = now;
    rawToScreen(rx, ry, lastSx, lastSy);
  } else if (down && now - lastSeen > 60) {
    down = false;
    if (hit(BTN_TONE, lastSx, lastSy)) {
      toneOn = !toneOn;
      drawButtons();
    } else if (hit(BTN_VDN, lastSx, lastSy)) {
      volumePct = max(0, volumePct - 10);
      drawButtons();
    } else if (hit(BTN_VUP, lastSx, lastSy)) {
      volumePct = min(100, volumePct + 10);
      drawButtons();
    } else if (lastSy >= ROW_Y0 && lastSy < ROW_Y0 + ROW_H * MAX_DEVS && !connected) {
      const int row = (lastSy - ROW_Y0) / ROW_H;
      portENTER_CRITICAL(&devMux);
      if (row < devCount) { chosen = row; listDirty = true; }
      portEXIT_CRITICAL(&devMux);
      if (chosen == row)
        Serial.printf("ui: chose \"%s\" -- connecting on next sighting\n", devs[row].name);
      stateDirty = true;
    }
  }

  if (listDirty) drawList();
  if (stateDirty) { stateDirty = false; drawHeader(); }

  static uint32_t lastLog = 0;
  static uint32_t lastFrames = 0;
  if (now - lastLog >= 5000) {
    const uint32_t f = framesServed;
    Serial.printf("stat: heap %u  min %u  frames/s %u  %s\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  (f - lastFrames) * 1000 / (now - lastLog),
                  connected ? "connected" : "not connected");
    lastFrames = f;
    lastLog = now;
  }
  delay(10);
}
