// ===========================================================================
// Stage 0.4 -- animated GIF background while MP3 streams to Bluetooth
// ===========================================================================
// The last hardware risk before the app (plan §4.9): can the UI loop decode
// and draw a GIF from SD while the decode task streams MP3 from the same card,
// without the audio dropping out or the heap running dry?
//
// Audio path is s03's, unchanged (and the speaker saved by s03 is reused).
//
// Layout, landscape 320x240:
//
//   +------------------------+------+
//   |                        | |<<  |
//   |   GIF, 240x240         | >||  |
//   |   (240x320 GIFs are    | >>|  |
//   |    centre-cropped)     | Vol- |
//   |                        | Vol+ |
//   |[ title band - MASKED ] |  BG  |
//   +------------------------+------+
//
// The title band sits ON TOP of the GIF area. The GIF draw callback skips
// every pixel inside it, so the text never flickers -- that masking is the
// technique the app's layouts rely on, and this sketch is where it is proven.
//
// BG button: next /bg/bgN.gif, then OFF, then back to bg1.
// Serial every 5 s adds: gif fps, average frame decode+draw ms, frames delayed
// because the audio buffer was low.
// ===========================================================================

#include <AnimatedGIF.h>

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
// GIF background
// ---------------------------------------------------------------------------
// Global, not heap: AnimatedGIF embeds its ~25 KB of decoder state in the
// object. As a static it is carved out before Bluetooth fragments the heap,
// so it can never fail to allocate mid-session.
static AnimatedGIF gif;
static File        gifFile;
static bool        gifOpen = false;
static int         bgIndex = 1;           // bgN.gif; 0 = off
static int         bgCount = 0;           // bg1..bgN that exist and are valid
static int         gifOffX = 0, gifOffY = 0;   // canvas -> screen
static uint32_t    gifNextAt = 0;
static uint32_t    gifFrames = 0, gifDrawMs = 0, gifDeferred = 0;

static const int GIF_AREA_W = 240, GIF_AREA_H = 240;
static const int FPS_CAP = 15;

struct Rect { int x, y, w, h; };
// UI regions the GIF must never paint over.
static const Rect MASKS[] = {
  { 0, 204, 240, 36 },                    // title band
};
static const int MASK_COUNT = sizeof(MASKS) / sizeof(MASKS[0]);

static void* gifOpenCb(const char* name, int32_t* size) {
  gifFile = SD.open(name);
  if (!gifFile) return nullptr;
  *size = gifFile.size();
  return &gifFile;
}

static void gifCloseCb(void* h) {
  static_cast<File*>(h)->close();
}

static int32_t gifReadCb(GIFFILE* pf, uint8_t* buf, int32_t len) {
  File* f = static_cast<File*>(pf->fHandle);
  int32_t n = len;
  if (pf->iSize - pf->iPos < len) n = pf->iSize - pf->iPos - 1;  // library quirk, as in its examples
  if (n <= 0) return 0;
  n = f->read(buf, n);
  pf->iPos = f->position();
  return n;
}

static int32_t gifSeekCb(GIFFILE* pf, int32_t pos) {
  File* f = static_cast<File*>(pf->fHandle);
  f->seek(pos);
  pf->iPos = (int32_t)f->position();
  return pf->iPos;
}

// Push screen pixels [x0, x1) of row y, minus any masked spans.
static void pushSpanMasked(int y, int x0, int x1, uint16_t* px) {
  int x = x0;
  while (x < x1) {
    int end = x1;
    bool masked = false;
    for (int m = 0; m < MASK_COUNT; m++) {
      const Rect& r = MASKS[m];
      if (y < r.y || y >= r.y + r.h) continue;
      if (x >= r.x && x < r.x + r.w) { masked = true; end = min(end, r.x + r.w); break; }
      if (r.x > x) end = min(end, r.x);
    }
    if (!masked) tft.pushImage(x, y, end - x, 1, px + (x - x0));
    x = end;
  }
}

static void gifDrawCb(GIFDRAW* d) {
  const int y = gifOffY + d->iY + d->y;
  if (y < 0 || y >= GIF_AREA_H) return;
  uint8_t* s = d->pPixels;
  const uint16_t* pal = d->pPalette;
  static uint16_t line[480];

  if (d->ucDisposalMethod == 2) {         // restore to background
    for (int i = 0; i < d->iWidth; i++)
      if (s[i] == d->ucTransparent) s[i] = d->ucBackground;
    d->ucHasTransparency = 0;
  }

  int sx = gifOffX + d->iX;               // screen x of source pixel 0
  int i0 = max(0, -sx), i1 = min(d->iWidth, GIF_AREA_W - sx);
  if (i0 >= i1) return;

  if (!d->ucHasTransparency) {
    for (int i = i0; i < i1; i++) line[i] = pal[s[i]];
    pushSpanMasked(y, sx + i0, sx + i1, line + i0);
    return;
  }
  // Transparent pixels keep what the previous frame left: push opaque runs only.
  int i = i0;
  while (i < i1) {
    while (i < i1 && s[i] == d->ucTransparent) i++;
    const int start = i;
    while (i < i1 && s[i] != d->ucTransparent) { line[i] = pal[s[i]]; i++; }
    if (i > start) pushSpanMasked(y, sx + start, sx + i, line + start);
  }
}

static void gifClose() {
  if (gifOpen) gif.close();
  gifOpen = false;
}

// Any GIF up to the decoder's 480 px limit is centred in the 240x240 area and
// cropped. (The plan said 240x240 / 240x320 only, but the first real files on
// the card were 250x250 -- rejecting a GIF over 5 px would be unfriendly.)
static bool gifStart(int n) {
  gifClose();
  tft.fillRect(0, 0, GIF_AREA_W, 204, TFT_BLACK);
  if (n <= 0) return false;
  char path[24];
  snprintf(path, sizeof(path), "/bg/bg%d.gif", n);
  const uint32_t h0 = ESP.getFreeHeap();
  if (!gif.open(path, gifOpenCb, gifCloseCb, gifReadCb, gifSeekCb, gifDrawCb)) {
    Serial.printf("gif: %s open failed (err %d)\n", path, gif.getLastError());
    return false;
  }
  const int w = gif.getCanvasWidth(), h = gif.getCanvasHeight();
  if (w > 480 || h > 480) {
    Serial.printf("gif: %s is %dx%d -- skipped (max 480x480)\n", path, w, h);
    gif.close();
    return false;
  }
  gifOffX = (GIF_AREA_W - w) / 2;         // negative = centre crop
  gifOffY = (GIF_AREA_H - h) / 2;
  gifOpen = true;
  gifNextAt = 0;
  Serial.printf("gif: %s %dx%d  frame delay ?  heap -%d (file handle)\n", path, w, h,
                (int)(h0 - ESP.getFreeHeap()));
  return true;
}

static int countBackgrounds() {
  int n = 0;
  char path[24];
  for (int i = 1; i < 100; i++) {
    snprintf(path, sizeof(path), "/bg/bg%d.gif", i);
    if (!SD.exists(path)) break;
    n = i;
  }
  return n;
}

static void gifTick(uint32_t now) {
  if (!gifOpen || now < gifNextAt) return;
  // Audio first: if the decode task is behind, give it this slice.
  if (connected && playing && rbUsed() < RB_FRAMES / 2) {
    gifDeferred++;
    gifNextAt = now + 20;
    return;
  }
  int delayMs = 0;
  const uint32_t t0 = millis();
  tft.startWrite();
  const int rc = gif.playFrame(false, &delayMs);
  tft.endWrite();
  const uint32_t took = millis() - t0;
  gifFrames++;
  gifDrawMs += took;
  if (rc == 0) gif.reset();               // loop the animation
  if (rc < 0) { Serial.printf("gif: decode error %d\n", gif.getLastError()); gifClose(); return; }
  const int minGap = 1000 / FPS_CAP;
  gifNextAt = t0 + max(delayMs, minGap);
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
static const Btn B_PREV = { 244,   2, 74, 36, "|<<" };
static const Btn B_PLAY = { 244,  42, 74, 36, ">||" };
static const Btn B_NEXT = { 244,  82, 74, 36, ">>|" };
static const Btn B_VDN  = { 244, 122, 74, 36, "Vol-" };
static const Btn B_VUP  = { 244, 162, 74, 36, "Vol+" };
static const Btn B_BG   = { 244, 202, 74, 36, "BG" };
static const Btn B_TITLE = { 0, 204, 240, 36, "" };
static const int ROW_Y0 = 30, ROW_H = 26;

static bool hit(const Btn& b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static void drawBtn(const Btn& b, uint16_t bg, const String& label) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2, 2);
}

static void drawSpeakerList() {
  BtDev copy[MAX_DEVS];
  int n;
  portENTER_CRITICAL(&devMux);
  n = devCount;
  memcpy(copy, devs, sizeof(copy));
  listDirty = false;
  portEXIT_CRITICAL(&devMux);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(wantName.length() ? "connecting to " + wantName + "..."
                                   : "tap your speaker (pairing mode)", 6, 12, 2);
  for (int i = 0; i < n; i++) {
    const int y = ROW_Y0 + i * ROW_H;
    const uint16_t bg = (wantName == copy[i].name) ? TFT_MAROON : 0x2104;
    tft.fillRoundRect(4, y + 1, tft.width() - 8, ROW_H - 3, 4, bg);
    tft.setTextColor(TFT_WHITE, bg);
    tft.drawString(copy[i].name, 10, y + ROW_H / 2, 2);
  }
}

static const uint16_t BAND_BG = 0x18E3;

static void drawButtons() {
  drawBtn(B_PREV, TFT_DARKGREY, B_PREV.label);
  drawBtn(B_PLAY, playing ? TFT_DARKGREEN : TFT_MAROON, playing ? "Pause" : "Play");
  drawBtn(B_NEXT, TFT_DARKGREY, B_NEXT.label);
  drawBtn(B_VDN, TFT_DARKGREY, B_VDN.label);
  drawBtn(B_VUP, TFT_DARKGREY, B_VUP.label);
  drawBtn(B_BG, TFT_NAVY, bgIndex ? "BG " + String(bgIndex) : "BG off");
}

static void drawBand() {
  const uint32_t sec = framesPlayed / 44100;
  String name = tracks[trackIdx];
  name = name.substring(name.lastIndexOf('/') + 1);
  if (name.length() > 4) name = name.substring(0, name.length() - 4);
  tft.fillRect(B_TITLE.x, B_TITLE.y, B_TITLE.w, B_TITLE.h, BAND_BG);
  tft.setTextColor(TFT_WHITE, BAND_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextWrap(false);
  tft.setViewport(B_TITLE.x, B_TITLE.y, B_TITLE.w, B_TITLE.h);
  tft.drawString(name, 4, 2, 2);
  tft.resetViewport();
  char b[40];
  snprintf(b, sizeof(b), "%u:%02u   vol %d%%", sec / 60, sec % 60, (int)volumePct);
  tft.setTextColor(TFT_CYAN, BAND_BG);
  tft.drawString(b, 4, B_TITLE.y + 19, 2);
}

static void requestTrack(int idx) {
  trackIdx = (idx + tracks.size()) % tracks.size();
  skipRequested = true;
}

static void nextBackground() {
  if (bgCount == 0) { bgIndex = 0; return; }
  // Try each remaining file once; invalid ones are skipped, then OFF.
  for (int tries = 0; tries <= bgCount; tries++) {
    bgIndex = (bgIndex >= bgCount) ? 0 : bgIndex + 1;
    if (bgIndex == 0) { gifStart(0); return; }
    if (gifStart(bgIndex)) return;
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== s04 GIF background + MP3 to Bluetooth ===");
  cydPrintBanner();
  Serial.printf("heap at setup: %u  largest %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  cydRgbLedOff();
  tft.init();
  tft.setRotation(CYD_ROTATION);
  tft.invertDisplay(CYD_TFT_INVERT);
  tft.fillScreen(TFT_BLACK);
  cydBacklightOn();
  xptBegin();

  sdSPI.begin(CYD_SD_SCK_PIN, CYD_SD_MISO_PIN, CYD_SD_MOSI_PIN, CYD_SD_CS_PIN);
  // max_files 3: the playing track, the GIF, one spare.
  if (!SD.begin(CYD_SD_CS_PIN, sdSPI, 20000000, "/sd", 3)) {
    Serial.println("sd: mount FAILED");
    tft.drawString("SD mount failed", 10, 100, 4);
    for (;;) delay(1000);
  }
  scanDir("/music", 0);
  std::sort(tracks.begin(), tracks.end());
  bgCount = countBackgrounds();
  Serial.printf("sd: %u tracks, %d backgrounds\n", (unsigned)tracks.size(), bgCount);
  if (tracks.empty()) {
    tft.drawString("no MP3s in /music", 10, 100, 4);
    for (;;) delay(1000);
  }

  // Report every background's size up front, so a bad file shows in the log
  // even if nobody cycles to it.
  gif.begin(BIG_ENDIAN_PIXELS);
  for (int i = 1; i <= bgCount; i++) { gifStart(i); gifClose(); }

  prefs.begin("s03", false);                // same speaker as s03
  wantName = prefs.getString("speaker", "");
  prefs.end();

  rb = (int16_t*)malloc(RB_FRAMES * 2 * sizeof(int16_t));
  Serial.printf("heap after sd+ring: %u  largest %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  a2dp.set_local_name("CYD-MP3");
  a2dp.set_ssid_callback(onSsid);
  a2dp.set_on_connection_state_changed(onConnState);
  a2dp.set_data_callback_in_frames(getFrames);
  a2dp.set_auto_reconnect(false);
  a2dp.set_volume(100);
  a2dp.start();
  Serial.printf("heap after bt start: %u\n", ESP.getFreeHeap());

  xTaskCreatePinnedToCore(decodeTask, "mp3dec", 4096, nullptr, 5, &decodeHandle, 1);
}

static bool     down = false;
static uint32_t lastSeen = 0;
static int      lastSx = 0, lastSy = 0;
static bool     shownPlayer = false;

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
    if (!connected) {
      if (lastSy >= ROW_Y0) {
        const int row = (lastSy - ROW_Y0) / ROW_H;
        portENTER_CRITICAL(&devMux);
        if (row < devCount) wantName = devs[row].name;
        listDirty = true;
        portEXIT_CRITICAL(&devMux);
      }
    } else if (hit(B_PREV, lastSx, lastSy)) {
      requestTrack(framesPlayed > 3 * 44100 ? (int)trackIdx : trackIdx - 1);
    } else if (hit(B_NEXT, lastSx, lastSy)) {
      requestTrack(trackIdx + 1);
    } else if (hit(B_PLAY, lastSx, lastSy)) {
      playing = !playing;
      drawButtons();
    } else if (hit(B_VDN, lastSx, lastSy)) {
      volumePct = max(0, volumePct - 5);
    } else if (hit(B_VUP, lastSx, lastSy)) {
      volumePct = min(100, volumePct + 5);
    } else if (hit(B_BG, lastSx, lastSy)) {
      nextBackground();
      drawButtons();
    }
  }

  if (connected) {
    if (!shownPlayer || trackChanged) {
      trackChanged = false;
      if (!shownPlayer) {
        tft.fillScreen(TFT_BLACK);
        shownPlayer = true;
        if (bgIndex && !gifOpen && !gifStart(bgIndex)) nextBackground();
      }
      drawButtons();
      drawBand();
    }
    static uint32_t lastBand = 0;
    if (now - lastBand >= 1000) { lastBand = now; drawBand(); }
    gifTick(now);
  } else {
    if (shownPlayer) { shownPlayer = false; gifClose(); listDirty = true; }
    if (listDirty) drawSpeakerList();
  }

  static uint32_t lastLog = 0, lastFrames = 0;
  if (now - lastLog >= 5000) {
    const uint32_t f = framesServed;
    const uint32_t dt = now - lastLog;
    Serial.printf("stat: heap %u min %u largest %u  dec-stack %u  frames/s %u  underruns %u  "
                  "ring %u%%  gif %s fps %u.%u avg %ums deferred %u  %s\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                  decodeHandle ? (unsigned)uxTaskGetStackHighWaterMark(decodeHandle) : 0,
                  (f - lastFrames) * 1000 / dt, (unsigned)underruns,
                  (unsigned)(rbUsed() * 100 / RB_FRAMES),
                  gifOpen ? ("bg" + String(bgIndex)).c_str() : "off",
                  gifFrames * 1000 / dt, (gifFrames * 10000 / dt) % 10,
                  gifFrames ? gifDrawMs / gifFrames : 0, gifDeferred,
                  connected ? (playing ? "playing" : "paused") : "not connected");
    gifFrames = gifDrawMs = gifDeferred = 0;
    lastFrames = f;
    lastLog = now;
  }
  delay(1);
}
