// ===========================================================================
// Stage 0.3 -- MP3 from SD to a Bluetooth speaker, with touch controls
// ===========================================================================
// The whole audio path of the player, minus the UI polish (plan §4.2):
//
//   SD file --(decode task, core 1)--> Helix MP3 --> resample to 44.1k stereo
//          --> ring buffer --> A2DP data callback (BT task) --> speaker
//
// The Bluetooth callback only ever copies out of the ring buffer; all SD and
// decoding work stays on the decode task. The UI loop reads touch at the same
// time, which is the "touch while SD is busy" check s01 could not settle.
//
// Use:
//   - First boot: the speaker list from s02. Tap your speaker (pairing mode).
//     Its name is remembered in NVS, so later boots connect by themselves.
//   - Player: |<<   >||   >>|   Vol-   Vol+  . Long-press the title to go back
//     to the speaker list and forget the saved speaker.
//
// Serial every 5 s:
//   stat: heap / min  frames/s (~44100)  underruns  ring fill  src rate/ch/kbps
// Healthy = underruns stays 0 while playing, frames/s ~44100.
// ===========================================================================

#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include "BluetoothA2DPSource.h"
#include "MP3DecoderHelix.h"
#include "board.h"
#include "btn.h"

#ifndef USE_HSPI_PORT
  #error "User_Setup.h is missing '#define USE_HSPI_PORT'. Copy config/User_Setup_2432S028R_ST7789.h.template into the TFT_eSPI library."
#endif

TFT_eSPI tft;
SPIClass sdSPI(VSPI);
BluetoothA2DPSource a2dp;
Preferences prefs;

// ---------------------------------------------------------------------------
// Bit-banged XPT2046 -- as verified in s01
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
// PCM ring buffer: single producer (decode task), single consumer (BT task)
// ---------------------------------------------------------------------------
// Stereo int16 frames at 44.1 kHz. 2048 frames = 46 ms, 16 KB -- enough to
// ride out an SD read or a slow frame decode, small enough to leave heap for
// the Bluetooth stack on a board with no PSRAM.
static const uint32_t RB_FRAMES = 2048;
static int16_t*        rb = nullptr;           // RB_FRAMES * 2 samples
static volatile uint32_t rbHead = 0;           // written by decode task only
static volatile uint32_t rbTail = 0;           // written by BT task only

// Emptying the buffer moves rbTail, which only the consumer may write. The
// decode task asks, and the BT callback does it on its next pull.
static volatile bool   rbFlushReq = false;

static inline uint32_t rbUsed()  { return (rbHead - rbTail) & (RB_FRAMES - 1); }
static inline uint32_t rbFree()  { return RB_FRAMES - 1 - rbUsed(); }

// ---------------------------------------------------------------------------
// Player state
// ---------------------------------------------------------------------------
static std::vector<String> tracks;
static volatile int      trackIdx = 0;
static volatile bool     playing = true;
static volatile int      volumePct = 40;
static volatile bool     skipRequested = false;   // decode task reopens trackIdx
static volatile bool     connected = false;
static volatile uint32_t framesServed = 0;        // all frames handed to BT
static volatile uint32_t framesPlayed = 0;        // real audio frames, this track
static volatile uint32_t underruns = 0;
static volatile bool     trackChanged = true;     // UI redraw flag
static MP3FrameInfo      srcInfo = {};

// ---------------------------------------------------------------------------
// Bluetooth
// ---------------------------------------------------------------------------
struct BtDev { char name[33]; int rssi; };
static const int MAX_DEVS = 6;
static BtDev      devs[MAX_DEVS];
static int        devCount = 0;
static String     wantName;                       // from NVS or a tap
static volatile bool listDirty = true;
static portMUX_TYPE devMux = portMUX_INITIALIZER_UNLOCKED;

static bool onSsid(const char* ssid, esp_bd_addr_t, int rssi) {
  const char* name = (ssid && *ssid) ? ssid : "(no name)";
  portENTER_CRITICAL(&devMux);
  int i = 0;
  for (; i < devCount; i++) if (strcmp(devs[i].name, name) == 0) break;
  if (i == devCount && devCount < MAX_DEVS) devCount++;
  if (i < devCount) {
    strlcpy(devs[i].name, name, sizeof(devs[i].name));
    devs[i].rssi = rssi;
    listDirty = true;
  }
  portEXIT_CRITICAL(&devMux);
  const bool go = wantName.length() && wantName == name;
  Serial.printf("bt: found \"%s\" rssi %d%s\n", name, rssi, go ? "  -> CONNECT" : "");
  return go;
}

static void onConnState(esp_a2d_connection_state_t state, void*) {
  static const char* NAMES[] = { "disconnected", "connecting", "connected", "disconnecting" };
  Serial.printf("bt: state %s\n", state <= 3 ? NAMES[state] : "?");
  const bool was = connected;
  connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
  if (connected && !was) {
    prefs.begin("s03", false);
    prefs.putString("speaker", wantName);
    prefs.end();
  }
  trackChanged = true;                           // full redraw
}

// BT task. Copy only -- no SD, no decoding, no logging.
static int32_t getFrames(Frame* frame, int32_t count) {
  int32_t i = 0;
  if (rbFlushReq) { rbTail = rbHead; rbFlushReq = false; }
  if (playing) {
    const int32_t gain = volumePct * volumePct;  // perceptual-ish curve, /10000
    uint32_t tail = rbTail;
    const uint32_t avail = (rbHead - tail) & (RB_FRAMES - 1);
    const int32_t n = min<int32_t>(count, avail);
    for (; i < n; i++) {
      frame[i].channel1 = (int16_t)((int32_t)rb[tail * 2]     * gain / 10000);
      frame[i].channel2 = (int16_t)((int32_t)rb[tail * 2 + 1] * gain / 10000);
      tail = (tail + 1) & (RB_FRAMES - 1);
    }
    rbTail = tail;
    framesPlayed += n;
    if (n < count && !skipRequested) underruns++;
  }
  for (; i < count; i++) frame[i].channel1 = frame[i].channel2 = 0;
  framesServed += count;
  return count;
}

// ---------------------------------------------------------------------------
// Decode task
// ---------------------------------------------------------------------------
// Linear resampler to 44.1 kHz stereo. Most MP3s are already 44.1 kHz and take
// the straight-copy branch; 48 kHz / 32 kHz / 22.05 kHz get interpolated. Fine
// for a test and inaudible on a BT speaker; the app can swap in something
// better if it ever matters.
static uint32_t rsPos = 0;                        // 16.16 fixed-point phase
static int16_t  rsPrevL = 0, rsPrevR = 0;

static void pushFrame(int16_t l, int16_t r) {
  while (rbFree() == 0) {
    if (skipRequested) return;                    // drop, a new track is coming
    vTaskDelay(pdMS_TO_TICKS(4));                 // backpressure from BT
  }
  const uint32_t h = rbHead;
  rb[h * 2] = l;
  rb[h * 2 + 1] = r;
  rbHead = (h + 1) & (RB_FRAMES - 1);
}

static void onPcm(MP3FrameInfo& info, short* pcm, size_t len, void*) {
  srcInfo = info;
  const int ch = info.nChans;
  const size_t frames = len / ch;
  if (info.samprate == 44100) {
    for (size_t i = 0; i < frames; i++) {
      const int16_t l = pcm[i * ch];
      pushFrame(l, ch == 2 ? pcm[i * ch + 1] : l);
    }
    return;
  }
  const uint32_t step = (uint32_t)(((uint64_t)info.samprate << 16) / 44100);
  for (size_t i = 0; i < frames; i++) {
    const int16_t l = pcm[i * ch];
    const int16_t r = ch == 2 ? pcm[i * ch + 1] : l;
    // Emit every output sample that falls between prev (0) and this one (1<<16).
    while (rsPos < 65536) {
      const int32_t f = rsPos;
      pushFrame(rsPrevL + (((int32_t)l - rsPrevL) * f >> 16),
                rsPrevR + (((int32_t)r - rsPrevR) * f >> 16));
      rsPos += step;
    }
    rsPos -= 65536;
    rsPrevL = l; rsPrevR = r;
  }
}

libhelix::MP3DecoderHelix mp3(onPcm);

// ID3v2 tags (often with embedded cover art) sit before the first frame. Helix
// would hunt through them for a sync word and can lock onto a false one inside
// a JPEG, which sounds like a click or a burst of noise at track start.
static uint32_t id3v2Size(File& f) {
  uint8_t h[10];
  if (f.read(h, 10) != 10 || memcmp(h, "ID3", 3) != 0) { f.seek(0); return 0; }
  uint32_t size = ((h[6] & 0x7F) << 21) | ((h[7] & 0x7F) << 14) |
                  ((h[8] & 0x7F) << 7) | (h[9] & 0x7F);
  size += 10 + ((h[5] & 0x10) ? 10 : 0);         // header + optional footer
  f.seek(size);
  return size;
}

static TaskHandle_t decodeHandle = nullptr;

static void decodeTask(void*) {
  static uint8_t buf[1024];
  File f;
  for (;;) {
    if (skipRequested || !f) {
      if (f) f.close();
      // Flush. If Bluetooth is not pulling (not connected), nothing else
      // touches rbTail and it is safe to do here.
      if (connected) {
        rbFlushReq = true;
        for (int i = 0; i < 50 && rbFlushReq; i++) vTaskDelay(pdMS_TO_TICKS(4));
      }
      if (rbFlushReq || !connected) { rbTail = rbHead; rbFlushReq = false; }
      skipRequested = false;
      framesPlayed = 0;
      rsPos = 0; rsPrevL = rsPrevR = 0;
      const uint32_t h0 = ESP.getFreeHeap();
      mp3.end();
      const uint32_t h1 = ESP.getFreeHeap();
      mp3.begin();
      const uint32_t h2 = ESP.getFreeHeap();
      f = SD.open(tracks[trackIdx]);
      Serial.printf("mem: decoder end +%d  begin -%d  file open -%d\n",
                    (int)(h1 - h0), (int)(h1 - h2), (int)(h2 - ESP.getFreeHeap()));
      if (!f) {
        Serial.printf("dec: cannot open %s\n", tracks[trackIdx].c_str());
        trackIdx = (trackIdx + 1) % tracks.size();
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      const uint32_t skip = id3v2Size(f);
      Serial.printf("dec: [%d/%u] %s  %u KB  id3 %u B  heap %u\n", trackIdx + 1,
                    (unsigned)tracks.size(), tracks[trackIdx].c_str(),
                    (unsigned)(f.size() / 1024), skip, ESP.getFreeHeap());
      trackChanged = true;
    }
    if (!playing || !connected) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }

    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) {                                 // end of track: wait for drain, next
      while (rbUsed() > 0 && !skipRequested) vTaskDelay(pdMS_TO_TICKS(10));
      if (!skipRequested) {
        trackIdx = (trackIdx + 1) % tracks.size();
        skipRequested = true;
      }
      continue;
    }
    mp3.write(buf, n);
  }
}

// ---------------------------------------------------------------------------
// SD
// ---------------------------------------------------------------------------
static void scanDir(const String& path, int depth) {
  File d = SD.open(path);
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    String p = path + "/" + f.name();
    if (f.isDirectory()) {
      if (depth < 2 && f.name()[0] != '.') scanDir(p, depth + 1);
    } else {
      String low = p;
      low.toLowerCase();
      if (low.endsWith(".mp3") && f.name()[0] != '.') tracks.push_back(p);
    }
    f.close();
  }
  d.close();
}

// ---------------------------------------------------------------------------
// UI (landscape 320x240)
// ---------------------------------------------------------------------------
static const int ROW_Y0 = 30, ROW_H = 26;
static const Btn B_PREV = {   4, 186, 60, 50, "|<<" };
static const Btn B_PLAY = {  68, 186, 60, 50, ">||" };
static const Btn B_NEXT = { 132, 186, 60, 50, ">>|" };
static const Btn B_VDN  = { 196, 186, 58, 50, "Vol-" };
static const Btn B_VUP  = { 258, 186, 58, 50, "Vol+" };
static const Btn B_TITLE = { 0, 24, 320, 60, "" };

static bool hit(const Btn& b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static void drawBtn(const Btn& b, uint16_t bg, const String& label) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2, 2);
}

static void drawHeader() {
  const uint16_t bg = connected ? TFT_DARKGREEN : TFT_NAVY;
  tft.fillRect(0, 0, tft.width(), 22, bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(ML_DATUM);
  String s = connected ? "BT: " + wantName
                       : (wantName.length() ? "connecting to " + wantName + "..."
                                            : "tap your speaker (pairing mode)");
  tft.drawString(s, 6, 11, 2);
}

static void drawSpeakerList() {
  BtDev copy[MAX_DEVS];
  int n;
  portENTER_CRITICAL(&devMux);
  n = devCount;
  memcpy(copy, devs, sizeof(copy));
  listDirty = false;
  portEXIT_CRITICAL(&devMux);
  tft.fillRect(0, 24, tft.width(), tft.height() - 24, TFT_BLACK);
  for (int i = 0; i < n; i++) {
    const int y = ROW_Y0 + i * ROW_H;
    const uint16_t bg = (wantName == copy[i].name) ? TFT_MAROON : 0x2104;
    tft.fillRoundRect(4, y + 1, tft.width() - 8, ROW_H - 3, 4, bg);
    tft.setTextColor(TFT_WHITE, bg);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(copy[i].name, 10, y + ROW_H / 2, 2);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(String(copy[i].rssi) + " dBm", tft.width() - 10, y + ROW_H / 2, 2);
  }
}

static String fmtTime(uint32_t s) {
  char b[12];
  snprintf(b, sizeof(b), "%u:%02u", s / 60, s % 60);
  return b;
}

static void drawPlayer() {
  tft.fillRect(0, 24, tft.width(), tft.height() - 24, TFT_BLACK);
  String name = tracks.empty() ? String("no MP3s in /music") : tracks[trackIdx];
  name = name.substring(name.lastIndexOf('/') + 1);
  if (name.length() > 4) name = name.substring(0, name.length() - 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextWrap(false);
  tft.drawString(name, 160, 50, name.length() <= 20 ? 4 : 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("track " + String(trackIdx + 1) + " / " + String(tracks.size()) +
                 "   (hold title: change speaker)", 160, 80, 1);
  drawBtn(B_PREV, TFT_DARKGREY, B_PREV.label);
  drawBtn(B_PLAY, playing ? TFT_DARKGREEN : TFT_MAROON, playing ? "Pause" : "Play");
  drawBtn(B_NEXT, TFT_DARKGREY, B_NEXT.label);
  drawBtn(B_VDN, TFT_DARKGREY, B_VDN.label);
  drawBtn(B_VUP, TFT_DARKGREY, B_VUP.label);
}

static void drawStatusLine() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  const uint32_t sec = framesPlayed / 44100;
  char b[64];
  snprintf(b, sizeof(b), "  %s    vol %d%%    %d Hz %s  ",
           fmtTime(sec).c_str(), (int)volumePct, srcInfo.samprate,
           srcInfo.nChans == 1 ? "mono" : "stereo");
  tft.drawString(b, 160, 120, 2);
  tft.setTextColor(underruns ? TFT_ORANGE : TFT_DARKGREY, TFT_BLACK);
  snprintf(b, sizeof(b), "  buffer %3u%%   underruns %u  ",
           (unsigned)(rbUsed() * 100 / RB_FRAMES), (unsigned)underruns);
  tft.drawString(b, 160, 150, 2);
}

static void requestTrack(int idx) {
  if (tracks.empty()) return;
  trackIdx = (idx + tracks.size()) % tracks.size();
  skipRequested = true;
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== s03 MP3 from SD to Bluetooth ===");
  cydPrintBanner();
  Serial.printf("heap at setup: %u  largest %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  cydRgbLedOff();
  tft.init();
  tft.setRotation(CYD_ROTATION);
  tft.invertDisplay(CYD_TFT_INVERT);
  tft.fillScreen(TFT_BLACK);
  cydBacklightOn();
  xptBegin();
  Serial.printf("heap after tft: %u\n", ESP.getFreeHeap());

  sdSPI.begin(CYD_SD_SCK_PIN, CYD_SD_MISO_PIN, CYD_SD_MOSI_PIN, CYD_SD_CS_PIN);
  // max_files 2: one track open for playback plus one spare. Each open file
  // costs a FIL object and a sector buffer.
  if (!SD.begin(CYD_SD_CS_PIN, sdSPI, 20000000, "/sd", 2)) {
    Serial.println("sd: mount FAILED");
    tft.drawString("SD mount failed", 10, 100, 4);
    for (;;) delay(1000);
  }
  scanDir("/music", 0);
  std::sort(tracks.begin(), tracks.end());
  Serial.printf("sd: %u tracks\n", (unsigned)tracks.size());
  if (tracks.empty()) {
    tft.drawString("no MP3s in /music", 10, 100, 4);
    for (;;) delay(1000);
  }

  prefs.begin("s03", false);
  wantName = prefs.getString("speaker", "");
  prefs.end();
  Serial.printf("bt: saved speaker \"%s\"\n", wantName.c_str());

  rb = (int16_t*)malloc(RB_FRAMES * 2 * sizeof(int16_t));
  Serial.printf("heap after sd+ring: %u  largest %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // The BLE half of the controller is never used; releasing it before the
  // controller starts hands its reserved RAM back to the heap.
  a2dp.set_reset_ble(true);
  a2dp.set_local_name("CYD-MP3");
  a2dp.set_ssid_callback(onSsid);
  a2dp.set_on_connection_state_changed(onConnState);
  a2dp.set_data_callback_in_frames(getFrames);
  a2dp.set_auto_reconnect(false);
  a2dp.set_volume(100);
  a2dp.start();
  Serial.printf("heap after bt start: %u\n", ESP.getFreeHeap());

  // Core 1, above loop() (priority 1) so the UI never starves decoding.
  xTaskCreatePinnedToCore(decodeTask, "mp3dec", 4096, nullptr, 5, &decodeHandle, 1);
}

static bool     down = false;
static uint32_t downAt = 0, lastSeen = 0;
static int      lastSx = 0, lastSy = 0;
static bool     shownPlayer = false;
static uint32_t touchSamplesWhilePlaying = 0;

void loop() {
  const uint32_t now = millis();

  // ---- touch ----
  int rx, ry, rz;
  xptRead(rx, ry, rz);
  if (rz > 400) {
    if (!down) { down = true; downAt = now; }
    lastSeen = now;
    rawToScreen(rx, ry, lastSx, lastSy);
    if (connected && playing) touchSamplesWhilePlaying++;
  } else if (down && now - lastSeen > 60) {
    down = false;
    const uint32_t held = lastSeen - downAt;
    if (!connected) {
      if (lastSy >= ROW_Y0) {
        const int row = (lastSy - ROW_Y0) / ROW_H;
        portENTER_CRITICAL(&devMux);
        if (row < devCount) wantName = devs[row].name;
        listDirty = true;
        portEXIT_CRITICAL(&devMux);
        Serial.printf("ui: speaker -> \"%s\"\n", wantName.c_str());
        drawHeader();
      }
    } else if (held > 1500 && hit(B_TITLE, lastSx, lastSy)) {
      Serial.println("ui: forget speaker");
      prefs.begin("s03", false); prefs.remove("speaker"); prefs.end();
      wantName = "";
      a2dp.disconnect();
    } else if (hit(B_PREV, lastSx, lastSy)) {
      // Like every player: restart the track unless we are near its start.
      requestTrack(framesPlayed > 3 * 44100 ? (int)trackIdx : trackIdx - 1);
    } else if (hit(B_NEXT, lastSx, lastSy)) {
      requestTrack(trackIdx + 1);
    } else if (hit(B_PLAY, lastSx, lastSy)) {
      playing = !playing;
      trackChanged = true;
    } else if (hit(B_VDN, lastSx, lastSy)) {
      volumePct = max(0, volumePct - 5);
    } else if (hit(B_VUP, lastSx, lastSy)) {
      volumePct = min(100, volumePct + 5);
    }
  }

  // ---- screen ----
  if (connected) {
    if (!shownPlayer || trackChanged) {
      trackChanged = false;
      shownPlayer = true;
      drawHeader();
      drawPlayer();
    }
    static uint32_t lastStatus = 0;
    if (now - lastStatus > 250) { lastStatus = now; drawStatusLine(); }
  } else {
    if (shownPlayer || trackChanged) {
      shownPlayer = false;
      trackChanged = false;
      drawHeader();
      listDirty = true;
    }
    if (listDirty) drawSpeakerList();
  }

  // ---- stats ----
  static uint32_t lastLog = 0, lastFrames = 0;
  if (now - lastLog >= 5000) {
    const uint32_t f = framesServed;
    Serial.printf("stat: heap %u min %u largest %u  dec-stack-free %u  frames/s %u  underruns %u  ring %u%%  "
                  "src %d Hz %dch %d kbps  touch-samples-while-playing %u  %s\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                  decodeHandle ? (unsigned)uxTaskGetStackHighWaterMark(decodeHandle) : 0,
                  (f - lastFrames) * 1000 / (now - lastLog), (unsigned)underruns,
                  (unsigned)(rbUsed() * 100 / RB_FRAMES), srcInfo.samprate,
                  srcInfo.nChans, srcInfo.bitrate / 1000,
                  (unsigned)touchSamplesWhilePlaying,
                  connected ? (playing ? "playing" : "paused") : "not connected");
    lastFrames = f;
    lastLog = now;
  }
  delay(5);
}
