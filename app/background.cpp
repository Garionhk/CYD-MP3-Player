// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "background.h"
#include "display.h"
#include "audio.h"
#include <SD.h>
#include <new>
#include "src/AnimatedGIF/AnimatedGIF.h"

// .anim format (little-endian), proven in stage0/s05:
//   header  "CANM" u8 ver=2 u8 0 u16 w u16 h u16 frames u32 srcSize u32 srcTime
//   frame   u16 delayMs, then spans (u16 y, u16 x, u16 n, n x u16 RGB565 in
//           panel byte order), terminated by y = 0xFFFF
static const uint16_t END_OF_FRAME = 0xFFFF;
// 2: frames hold only the pixels that change (version 1 held every painted
// pixel). Files of another version are converted again.
static const uint8_t  ANIM_VERSION = 2;

struct AnimHeader {
  char     magic[4];
  uint8_t  ver, pad;
  uint16_t w, h, frames;
  uint32_t srcSize, srcTime;
} __attribute__((packed));

static int bgCount = 0;

// ===========================================================================
// Converter
// ===========================================================================
struct Conv {
  File      in, out;
  int       tw, th;
  uint16_t* xmap;          // target x -> source canvas x
  uint16_t* rowFirst;      // source canvas row -> first target row
  uint16_t* rowCount;      //   ... and how many target rows it covers
  uint8_t*  wbuf;
  int       wlen;
  uint32_t  bytes;
  bool      failed;
  // What the screen will show after the frames written so far, one row per
  // target row: tw pixels (RGB565, panel byte order) then tw "known" bits.
  // Lets each frame store only the pixels that actually change. Null when it
  // could not be allocated -- frames are then stored whole, as in format 1.
  uint8_t** canvas;
};
static Conv conv;
static const int WBUF = 2048;

static void convWrite(const void* data, int len) {
  const uint8_t* p = (const uint8_t*)data;
  while (len > 0) {
    const int n = min(len, WBUF - conv.wlen);
    memcpy(conv.wbuf + conv.wlen, p, n);
    conv.wlen += n; p += n; len -= n;
    if (conv.wlen == WBUF) {
      if (conv.out.write(conv.wbuf, conv.wlen) != (size_t)conv.wlen) conv.failed = true;
      conv.bytes += conv.wlen;
      conv.wlen = 0;
    }
  }
}

static void convFlush() {
  if (conv.wlen && conv.out.write(conv.wbuf, conv.wlen) != (size_t)conv.wlen) conv.failed = true;
  conv.bytes += conv.wlen;
  conv.wlen = 0;
}

static void* convOpenCb(const char* name, int32_t* size) {
  conv.in = SD.open(name);
  if (!conv.in) return nullptr;
  *size = conv.in.size();
  return &conv.in;
}
static void convCloseCb(void* h) { static_cast<File*>(h)->close(); }
static int32_t convReadCb(GIFFILE* pf, uint8_t* buf, int32_t len) {
  File* f = static_cast<File*>(pf->fHandle);
  int32_t n = len;
  if (pf->iSize - pf->iPos < len) n = pf->iSize - pf->iPos - 1;   // as the library's examples
  if (n <= 0) return 0;
  n = f->read(buf, n);
  pf->iPos = f->position();
  return n;
}
static int32_t convSeekCb(GIFFILE* pf, int32_t pos) {
  File* f = static_cast<File*>(pf->fHandle);
  f->seek(pos);
  pf->iPos = (int32_t)f->position();
  return pf->iPos;
}

static inline int  rowBytes()                           { return conv.tw * 2 + (conv.tw + 7) / 8; }
static inline bool known(const uint8_t* row, int x)     { return row[conv.tw * 2 + (x >> 3)] & (1 << (x & 7)); }
static inline uint16_t stored(const uint8_t* row, int x){ return row[x * 2] | (row[x * 2 + 1] << 8); }

// Gaps of up to this many unchanged-but-known pixels are drawn anyway: a new
// span header costs 6 bytes, redrawing a pixel with the colour it already has
// costs 2 and changes nothing on screen.
static const int MERGE_GAP = 3;

static void convDrawCb(GIFDRAW* d) {
  const int srcY = d->iY + d->y;
  const int rows = conv.rowCount[srcY];
  if (rows == 0) return;
  uint8_t* s = d->pPixels;
  if (d->ucDisposalMethod == 2) {                   // restore to background
    for (int i = 0; i < d->iWidth; i++)
      if (s[i] == d->ucTransparent) s[i] = d->ucBackground;
    d->ucHasTransparency = 0;
  }

  // What this frame paints on the row: colour, or -1 for "leave as is".
  static int32_t paint[240];
  for (int tx = 0; tx < conv.tw; tx++) {
    const int sx = conv.xmap[tx] - d->iX;
    const bool painted = sx >= 0 && sx < d->iWidth &&
                         !(d->ucHasTransparency && s[sx] == d->ucTransparent);
    paint[tx] = painted ? d->pPalette[s[sx]] : -1;
  }

  static uint16_t line[240];
  for (int r = 0; r < rows; r++) {
    const uint16_t ty = conv.rowFirst[srcY] + r;
    uint8_t* row = conv.canvas ? conv.canvas[ty] : nullptr;

    // emit[tx]: this pixel must be drawn (painted and different, or unknown).
    int tx = 0;
    while (tx < conv.tw) {
      const auto emit = [&](int x) {
        if (paint[x] < 0) return false;
        if (!row) return true;
        return !known(row, x) || stored(row, x) != (uint16_t)paint[x];
      };
      while (tx < conv.tw && !emit(tx)) tx++;
      if (tx >= conv.tw) break;
      const int start = tx;
      int end = tx;                                // exclusive end of the span
      while (end < conv.tw) {
        if (emit(end)) { end++; continue; }
        // Bridge a short gap of pixels whose on-screen colour is known.
        int g = end;
        while (g < conv.tw && g - end < MERGE_GAP && !emit(g) && row &&
               (paint[g] >= 0 || known(row, g))) g++;
        if (g < conv.tw && g > end && emit(g)) { end = g; continue; }
        break;
      }
      int n = 0;
      for (int x = start; x < end; x++) {
        const uint16_t c = paint[x] >= 0 ? (uint16_t)paint[x] : stored(row, x);
        line[n++] = c;
        if (row) {
          row[x * 2] = c & 0xFF;
          row[x * 2 + 1] = c >> 8;
          row[conv.tw * 2 + (x >> 3)] |= 1 << (x & 7);
        }
      }
      const uint16_t hdr[3] = { ty, (uint16_t)start, (uint16_t)n };
      convWrite(hdr, sizeof(hdr));
      convWrite(line, n * 2);
      tx = end;
    }
  }
}

static void freeCanvas() {
  if (!conv.canvas) return;
  for (int y = 0; y < conv.th; y++) free(conv.canvas[y]);
  free(conv.canvas);
  conv.canvas = nullptr;
}

// One small allocation per row rather than one 100+ KB block: the heap before
// Bluetooth has the space but not in a single piece. It is still ~120 KB, so
// the allocation stops short of exhausting the heap -- the first attempt ran
// the board down to 10.5 KB free. Without the canvas, frames are stored whole.
static const uint32_t CANVAS_HEAP_FLOOR = 48 * 1024;

static void allocCanvas() {
  conv.canvas = (uint8_t**)calloc(conv.th, sizeof(uint8_t*));
  if (!conv.canvas) return;
  for (int y = 0; y < conv.th; y++) {
    if (ESP.getFreeHeap() < CANVAS_HEAP_FLOOR + rowBytes()) {
      Serial.printf("bg: only %u B heap -- storing whole frames\n", ESP.getFreeHeap());
      freeCanvas();
      return;
    }
    conv.canvas[y] = (uint8_t*)calloc(1, rowBytes());
    if (!conv.canvas[y]) {
      Serial.println("bg: no memory for frame differencing -- storing whole frames");
      freeCanvas();
      return;
    }
  }
}

static bool animIsCurrent(const char* animPath, File& src) {
  File a = SD.open(animPath);
  if (!a) return false;
  AnimHeader h;
  const bool ok = a.read((uint8_t*)&h, sizeof(h)) == sizeof(h) &&
                  memcmp(h.magic, "CANM", 4) == 0 && h.ver == ANIM_VERSION && h.frames > 0 &&
                  h.srcSize == (uint32_t)src.size() &&
                  h.srcTime == (uint32_t)src.getLastWrite();
  a.close();
  return ok;
}

static bool convertOne(int n, int total, BgProgressFn progress) {
  char gifPath[24], animPath[24], tmpPath[24];
  snprintf(gifPath, sizeof(gifPath), "/bg/bg%d.gif", n);
  snprintf(animPath, sizeof(animPath), "/.sys/bg%d.anim", n);
  snprintf(tmpPath, sizeof(tmpPath), "/.sys/bg%d.tmp", n);

  File src = SD.open(gifPath);
  if (!src) return false;
  if (animIsCurrent(animPath, src)) { src.close(); return true; }
  const uint32_t srcSize = src.size(), srcTime = (uint32_t)src.getLastWrite();
  src.close();
  if (progress) progress(n, total, 0);

  const uint32_t t0 = millis();
  AnimatedGIF* g = new (std::nothrow) AnimatedGIF();          // ~27 KB, freed before Bluetooth starts
  conv.wbuf = (uint8_t*)malloc(WBUF);
  if (!g || !conv.wbuf) {
    Serial.println("bg: no memory for the converter");
    delete g; free(conv.wbuf);
    return false;
  }
  g->begin(BIG_ENDIAN_PIXELS);
  conv.wlen = 0; conv.bytes = 0; conv.failed = false;
  if (!g->open(gifPath, convOpenCb, convCloseCb, convReadCb, convSeekCb, convDrawCb)) {
    Serial.printf("bg: %s cannot be read (gif error %d)\n", gifPath, g->getLastError());
    delete g; free(conv.wbuf);
    return false;
  }
  const int sw = g->getCanvasWidth(), sh = g->getCanvasHeight();
  conv.tw = 240;
  conv.th = (sh * 100 >= sw * 117) ? 320 : 240;           // tall -> 240x320
  const float scale = max((float)conv.tw / sw, (float)conv.th / sh);   // cover
  const float cx = (sw * scale - conv.tw) / 2, cy = (sh * scale - conv.th) / 2;
  conv.xmap     = (uint16_t*)malloc(conv.tw * sizeof(uint16_t));
  conv.rowFirst = (uint16_t*)calloc(sh, sizeof(uint16_t));
  conv.rowCount = (uint16_t*)calloc(sh, sizeof(uint16_t));
  for (int tx = 0; tx < conv.tw; tx++)
    conv.xmap[tx] = min(sw - 1, (int)((tx + cx) / scale));
  for (int ty = 0; ty < conv.th; ty++) {
    const int sy = min(sh - 1, (int)((ty + cy) / scale));
    if (conv.rowCount[sy] == 0) conv.rowFirst[sy] = ty;
    conv.rowCount[sy]++;
  }
  conv.canvas = nullptr;
  allocCanvas();

  SD.remove(tmpPath);
  conv.out = SD.open(tmpPath, FILE_WRITE);
  AnimHeader h = { { 'C', 'A', 'N', 'M' }, ANIM_VERSION, 0, (uint16_t)conv.tw, (uint16_t)conv.th, 0,
                   srcSize, srcTime };
  convWrite(&h, sizeof(h));
  Serial.printf("bg: converting %s %dx%d -> %dx%d\n", gifPath, sw, sh, conv.tw, conv.th);

  int frames = 0, delayMs = 0, rc = 1;
  while (rc > 0 && !conv.failed && frames < 1000) {
    // The frame's delay is only known once it has been decoded, so a
    // placeholder goes first and is patched afterwards.
    convFlush();
    const uint32_t frameAt = conv.bytes;
    const uint16_t placeholder = 0;
    convWrite(&placeholder, 2);
    rc = g->playFrame(false, &delayMs);
    const uint16_t end = END_OF_FRAME;
    convWrite(&end, 2);
    convFlush();
    const uint16_t dly = (uint16_t)constrain(delayMs, 20, 10000);
    conv.out.seek(frameAt);
    conv.out.write((uint8_t*)&dly, 2);
    conv.out.seek(conv.bytes);
    if (rc < 0) break;
    frames++;
    if (progress && frames % 5 == 0) progress(n, total, frames);
  }
  const int err = g->getLastError();
  g->close();
  delete g;
  free(conv.xmap); free(conv.rowFirst); free(conv.rowCount); free(conv.wbuf);
  freeCanvas();

  if (frames == 0 || conv.failed) {
    conv.out.close();
    SD.remove(tmpPath);
    Serial.printf("bg: %s FAILED after %d frames (gif error %d, write failed %d)\n",
                  gifPath, frames, err, conv.failed);
    return false;
  }
  h.frames = frames;
  conv.out.seek(0);
  conv.out.write((uint8_t*)&h, sizeof(h));
  conv.out.close();
  SD.remove(animPath);
  SD.rename(tmpPath, animPath);
  Serial.printf("bg: %s ready, %d frames, %u KB, %lu ms\n", animPath, frames,
                conv.bytes / 1024, millis() - t0);
  return true;
}

void bg_convertAll(BgProgressFn progress) {
  char path[24];
  bgCount = 0;
  for (int i = 1; i < 100; i++) {
    snprintf(path, sizeof(path), "/bg/bg%d.gif", i);
    if (!SD.exists(path)) break;
    bgCount = i;
  }
  for (int i = 1; i <= bgCount; i++) convertOne(i, bgCount, progress);
}

int bg_count() { return bgCount; }

// ===========================================================================
// Player
// ===========================================================================
static File        anim;
static bool        active = false;
static int         current = 0;
static Rect        view = { 0, 0, 240, 240 };
static const Rect* masks = nullptr;
static int         maskCount = 0;
static int         offX = 0, offY = 0;       // anim pixel -> screen
static uint32_t    nextAt = 0;
static uint32_t    framesDrawn = 0, drawMsTotal = 0;

static uint8_t rbuf[1024];                   // read-ahead over the .anim file
static int     rpos = 0, rlen = 0;

static const int FPS_CAP = 15;
static uint32_t diagMaxFrameMs = 0, diagMaxFrameBytes = 0;

static bool readBytes(void* dst, int len) {
  uint8_t* p = (uint8_t*)dst;
  while (len > 0) {
    if (rpos == rlen) {
      rlen = anim.read(rbuf, sizeof(rbuf));
      rpos = 0;
      if (rlen <= 0) { rlen = 0; return false; }
    }
    const int n = min(len, rlen - rpos);
    memcpy(p, rbuf + rpos, n);
    rpos += n; p += n; len -= n;
  }
  return true;
}

void bg_setView(const Rect& area, const Rect* m, int count) {
  view = area;
  masks = m;
  maskCount = count;
}

// Push screen pixels [x0, x1) of row y, clipped to the view, minus the masks.
static void pushSpan(int y, int x0, int x1, const uint16_t* px) {
  if (y < view.y || y >= view.y + view.h) return;
  const int cl = max(x0, view.x), cr = min(x1, view.x + view.w);
  int x = cl;
  while (x < cr) {
    int end = cr;
    bool masked = false;
    for (int m = 0; m < maskCount; m++) {
      const Rect& r = masks[m];
      if (y < r.y || y >= r.y + r.h) continue;
      if (x >= r.x && x < r.x + r.w) { masked = true; end = min(end, r.x + r.w); break; }
      if (r.x > x) end = min(end, r.x);
    }
    if (!masked) tft.pushImage(x, y, end - x, 1, (uint16_t*)(px + (x - x0)));
    x = end;
  }
}

void bg_stop() {
  if (anim) anim.close();
  active = false;
  current = 0;
}

bool bg_start(int n) {
  bg_stop();
  if (n <= 0 || n > bgCount) return false;
  char path[24];
  snprintf(path, sizeof(path), "/.sys/bg%d.anim", n);
  anim = SD.open(path);
  AnimHeader h;
  if (!anim || anim.read((uint8_t*)&h, sizeof(h)) != sizeof(h) ||
      memcmp(h.magic, "CANM", 4) != 0 || h.frames == 0) {
    if (anim) anim.close();
    Serial.printf("bg: %s missing or invalid\n", path);
    return false;
  }
  offX = view.x + (view.w - h.w) / 2;        // centre; negative = crop
  offY = view.y + (view.h - h.h) / 2;
  rpos = rlen = 0;
  active = true;
  current = n;
  nextAt = 0;
  return true;
}

bool bg_active()  { return active; }
int  bg_current() { return current; }

// Reading budget. The background shares the SD card with the decode task, and
// one owner's GIF converted to 114 KB frames at 11 fps -- more than the card
// delivers (~870 KB/s), which starved the audio. Pacing frames so the
// background reads at most this much per second keeps the card for the music;
// a heavy animation simply plays slower. (Drawing large frames in slices that
// pause when audio runs low was tried first and was worse: 212 underruns in 7
// minutes against 4 -- the card was still saturated, just in smaller pieces.)
static const uint32_t BG_MAX_BYTES_PER_S = 350000;

void bg_tick(uint32_t now) {
  if (!active || now < nextAt) return;
  if (audio_bufferLow()) { nextAt = now + 20; return; }   // audio first

  const uint32_t t0 = millis();
  uint16_t delayMs;
  if (!readBytes(&delayMs, 2)) {             // end of file: loop
    anim.seek(sizeof(AnimHeader));
    rpos = rlen = 0;
    if (!readBytes(&delayMs, 2)) { bg_stop(); return; }
  }
  const uint32_t startPos = anim.position() - (rlen - rpos);
  static uint16_t line[240];
  tft.startWrite();
  for (;;) {
    uint16_t hdr[3];
    if (!readBytes(hdr, 2) || hdr[0] == END_OF_FRAME) break;
    if (!readBytes(hdr + 1, 4) || hdr[2] > 240) break;     // corrupt: end frame
    const int n = hdr[2];
    if (!readBytes(line, n * 2)) break;
    pushSpan(hdr[0] + offY, hdr[1] + offX, hdr[1] + offX + n, line);
  }
  tft.endWrite();
  const uint32_t took = millis() - t0;
  const uint32_t bytes = (anim.position() - (rlen - rpos)) - startPos;
  framesDrawn++;
  drawMsTotal += took;
  diagMaxFrameMs = max<uint32_t>(diagMaxFrameMs, took);
  diagMaxFrameBytes = max<uint32_t>(diagMaxFrameBytes, bytes);
  const uint32_t budgetMs = (uint64_t)bytes * 1000 / BG_MAX_BYTES_PER_S;
  nextAt = t0 + max<uint32_t>(max<uint32_t>(delayMs, 1000 / FPS_CAP), budgetMs);
}

void bg_takeMax(uint32_t& ms, uint32_t& bytes) {
  ms = diagMaxFrameMs; bytes = diagMaxFrameBytes;
  diagMaxFrameMs = diagMaxFrameBytes = 0;
}

void bg_takeStats(uint32_t& frames, uint32_t& avgMs) {
  frames = framesDrawn;
  avgMs = framesDrawn ? drawMsTotal / framesDrawn : 0;
  framesDrawn = drawMsTotal = 0;
}

#endif  // !CYD_UPLOADER
