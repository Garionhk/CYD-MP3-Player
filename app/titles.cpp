#include "titles.h"
#include "display.h"
#include "storage.h"
#include "i18n.h"
#include "theme.h"
#include "style.h"
#include <SD.h>
#include <esp_rom_crc.h>
#include <vector>

// Strip file: u16 width, u8 height, u8 bits-per-pixel (2), then height rows of
// width pixels, 2 bits each, packed MSB-first, row-major.
static const char*  FONT_NAME  = ".sys/cjk16";        // TFT_eSPI adds "/" and ".vlw"
static const char*  FONT_PATH  = "/.sys/cjk16.vlw";
static const char*  STRIP_DIR  = "/.sys/t";
static const int    MAX_W      = 720;                 // ~45 CJK characters

static uint8_t* strip = nullptr;
static int      stripW = 0;
static uint32_t stripKey = 0;

static uint32_t keyFor(const String& text) {
  return esp_rom_crc32_le(0, (const uint8_t*)text.c_str(), text.length());
}

static String pathFor(uint32_t key) {
  char p[32];
  snprintf(p, sizeof(p), "%s/%08x.ttl", STRIP_DIR, key);
  return p;
}

static inline int stripBytes(int w) { return (w * TITLE_STRIP_H * 2 + 7) / 8; }

// ---------------------------------------------------------------------------
// Boot: render missing strips
// ---------------------------------------------------------------------------
static bool renderOne(TFT_eSprite& spr, const String& text, const String& path) {
  const int w = constrain(spr.textWidth(text), 1, MAX_W);
  spr.fillSprite(TFT_BLACK);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.drawString(text, 0, 0);

  const int n = stripBytes(w);
  uint8_t* buf = (uint8_t*)calloc(1, n);
  if (!buf) return false;
  for (int y = 0; y < TITLE_STRIP_H; y++) {
    for (int x = 0; x < w; x++) {
      // 8-bit sprite: white text on black, so red is the coverage. Top two
      // bits of it are the 4 anti-aliasing levels kept.
      const uint16_t c = spr.readPixel(x, y);
      const uint8_t level = ((c >> 11) & 0x1F) >> 3;
      const int bit = (y * w + x) * 2;
      buf[bit >> 3] |= level << (6 - (bit & 7));
    }
  }
  File f = SD.open(path, FILE_WRITE);
  bool ok = false;
  if (f) {
    const uint8_t hdr[4] = { (uint8_t)w, (uint8_t)(w >> 8), TITLE_STRIP_H, 2 };
    ok = f.write(hdr, 4) == 4 && f.write(buf, n) == (size_t)n;
    f.close();
  }
  free(buf);
  return ok;
}

// Boot-time pass state, shared with the storage_forEach callback.
static std::vector<String>* pending = nullptr;

static void collectMissing(int, const String&, const String& label) {
  if (!SD.exists(pathFor(keyFor(label)))) pending->push_back(label);
}

// A marker holding the library checksum the strips were last made for. When it
// matches, every title already has its strip and the per-title check -- one
// SD lookup per track -- is skipped.
static const char* DONE_PATH = "/.sys/t/done";

// Fixed UI text is part of the set too: a firmware update that adds or changes
// a Chinese string must not be skipped by an old marker.
static uint32_t uiCrc = 0;
static void crcOne(const char* text) {
  uiCrc = esp_rom_crc32_le(uiCrc, (const uint8_t*)text, strlen(text));
}

static String doneStamp() {
  uiCrc = 0;
  i18n_forEachChinese(crcOne);
  for (int i = 0; i < theme_count(); i++) crcOne(theme_at(i).nameZh);
  for (int i = 0; i < style_count(); i++) crcOne(style_at(i).nameZh);
  char b[20];
  snprintf(b, sizeof(b), "%08x%08x", storage_libraryCrc(), uiCrc);
  return b;
}

static bool upToDate() {
  File f = SD.open(DONE_PATH);
  if (!f) return false;
  const String s = f.readString();
  f.close();
  return s == doneStamp();
}

static void markDone() {
  File f = SD.open(DONE_PATH, FILE_WRITE);
  if (f) { f.print(doneStamp()); f.close(); }
}

static void collectUiText(const char* text) {
  const String s = text;
  if (!SD.exists(pathFor(keyFor(s)))) pending->push_back(s);
}

bool titles_prepareAll(TitleProgressFn progress) {
  if (!SD.exists(STRIP_DIR)) SD.mkdir(STRIP_DIR);
  if (upToDate()) return true;

  std::vector<String> missing;
  pending = &missing;
  storage_forEach(collectMissing);
  i18n_forEachChinese(collectUiText);
  for (int i = 0; i < theme_count(); i++) collectUiText(theme_at(i).nameZh);
  for (int i = 0; i < style_count(); i++) collectUiText(style_at(i).nameZh);
  pending = nullptr;
  if (missing.empty()) { markDone(); return true; }

  if (!SD.exists(FONT_PATH)) {
    Serial.printf("titles: %u titles need rendering but %s is missing -- ASCII fallback\n",
                  (unsigned)missing.size(), FONT_PATH);
    return false;
  }

  const uint32_t t0 = millis();
  TFT_eSprite spr(&tft);
  spr.setColorDepth(8);
  if (!spr.createSprite(MAX_W, TITLE_STRIP_H)) {
    Serial.println("titles: no memory for the render sprite");
    return false;
  }
  spr.loadFont(FONT_NAME, SD);
  Serial.printf("titles: font loaded, heap %u -- rendering %u titles\n",
                ESP.getFreeHeap(), (unsigned)missing.size());

  int done = 0, failed = 0;
  for (const String& text : missing) {
    if (!renderOne(spr, text, pathFor(keyFor(text)))) {
      failed++;
      Serial.printf("titles: could not write strip for \"%s\"\n", text.c_str());
    }
    if (progress) progress(++done, missing.size());
  }
  spr.unloadFont();
  spr.deleteSprite();
  if (!failed) markDone();
  Serial.printf("titles: %d rendered in %lu ms, heap %u\n", done, millis() - t0,
                ESP.getFreeHeap());
  return true;
}

// ---------------------------------------------------------------------------
// Playback: load and draw
// ---------------------------------------------------------------------------
bool titles_load(const String& text) {
  const uint32_t key = keyFor(text);
  if (strip && key == stripKey) return true;
  free(strip);
  strip = nullptr;
  stripW = 0;

  File f = SD.open(pathFor(key));
  if (!f) return false;
  uint8_t hdr[4];
  if (f.read(hdr, 4) == 4 && hdr[2] == TITLE_STRIP_H && hdr[3] == 2) {
    const int w = hdr[0] | (hdr[1] << 8);
    const int n = stripBytes(w);
    if (w > 0 && w <= MAX_W && (strip = (uint8_t*)malloc(n)) != nullptr) {
      if (f.read(strip, n) == (size_t)n) {
        stripW = w;
        stripKey = key;
      } else {
        free(strip);
        strip = nullptr;
      }
    }
  }
  f.close();
  return strip != nullptr;
}

bool titles_loaded() { return strip != nullptr; }
int  titles_width()  { return stripW; }

void titles_draw(const Rect& box, int scrollX, int gap, uint16_t fg, uint16_t bg) {
  if (!strip) return;
  // Four coverage levels blended between the two theme colours, byte-swapped
  // for pushImage (it sends the buffer as-is).
  uint16_t lut[4];
  for (int i = 0; i < 4; i++) {
    const uint16_t c = tft.alphaBlend(i * 85, fg, bg);
    lut[i] = (c >> 8) | (c << 8);
  }
  const int period = stripW + gap;
  static uint16_t line[320];
  const int w = min(box.w, 320);
  tft.startWrite();
  for (int y = 0; y < min(box.h, TITLE_STRIP_H); y++) {
    for (int x = 0; x < w; x++) {
      int vx = scrollX + x;
      if (vx >= period) vx %= period;
      uint8_t level = 0;
      if (vx < stripW) {
        const int bit = (y * stripW + vx) * 2;
        level = (strip[bit >> 3] >> (6 - (bit & 7))) & 3;
      }
      line[x] = lut[level];
    }
    tft.pushImage(box.x, box.y + y, w, 1, line);
  }
  // Anything under the strip's 18 rows belongs to the box too.
  if (box.h > TITLE_STRIP_H) {
    const uint16_t b = (bg >> 8) | (bg << 8);
    for (int x = 0; x < w; x++) line[x] = b;
    for (int y = TITLE_STRIP_H; y < box.h; y++) tft.pushImage(box.x, box.y + y, w, 1, line);
  }
  tft.endWrite();
}
