#include "id3.h"

static const size_t MAX_TEXT = 200;          // bytes of frame body worth reading

static uint32_t syncsafe(const uint8_t* p) {
  return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) | ((p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

static void appendUtf8(String& s, uint32_t cp) {
  if (cp == 0) return;
  char b[5] = { 0 };
  if (cp < 0x80)        { b[0] = cp; }
  else if (cp < 0x800)  { b[0] = 0xC0 | (cp >> 6);  b[1] = 0x80 | (cp & 0x3F); }
  else if (cp < 0x10000){ b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F);
                          b[2] = 0x80 | (cp & 0x3F); }
  else                  { b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 0x3F);
                          b[2] = 0x80 | ((cp >> 6) & 0x3F); b[3] = 0x80 | (cp & 0x3F); }
  s += b;
}

// Text frame body: one encoding byte, then the string (maybe NUL-terminated,
// maybe several NUL-separated values in v2.4 -- the first is kept).
static String decodeText(const uint8_t* p, size_t n) {
  String out;
  if (n < 2) return out;
  const uint8_t enc = p[0];
  p++; n--;
  if (enc == 0) {                              // ISO-8859-1
    for (size_t i = 0; i < n && p[i]; i++) appendUtf8(out, p[i]);
  } else if (enc == 3) {                       // UTF-8
    for (size_t i = 0; i < n && p[i]; i++) out += (char)p[i];
  } else {                                     // UTF-16, with BOM (1) or BE (2)
    bool be = (enc == 2);
    size_t i = 0;
    if (enc == 1 && n >= 2) {
      if (p[0] == 0xFE && p[1] == 0xFF) { be = true;  i = 2; }
      else if (p[0] == 0xFF && p[1] == 0xFE) { be = false; i = 2; }
    }
    for (; i + 1 < n; i += 2) {
      uint32_t u = be ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
      if (u == 0) break;
      if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {         // surrogate pair
        const uint32_t lo = be ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
      appendUtf8(out, u);
    }
  }
  out.trim();
  // A tab or newline in a title would break the library index's line format.
  out.replace('\t', ' ');
  out.replace('\n', ' ');
  out.replace('\r', ' ');
  return out;
}

bool id3_read(fs::File& f, Id3Info& out) {
  out = Id3Info();
  uint8_t h[10];
  f.seek(0);
  if (f.read(h, 10) != 10 || memcmp(h, "ID3", 3) != 0) return false;
  const uint8_t ver = h[3];
  if (ver < 2 || ver > 4) return false;
  const bool unsync = h[5] & 0x80;
  const uint32_t tagEnd = 10 + syncsafe(h + 6);

  uint32_t pos = 10;
  if ((h[5] & 0x40) && ver >= 3) {             // extended header: skip it
    uint8_t e[4];
    if (f.read(e, 4) != 4) return false;
    pos += (ver == 4) ? syncsafe(e) : ((e[0] << 24 | e[1] << 16 | e[2] << 8 | e[3]) + 4);
  }

  const int hdrLen = (ver == 2) ? 6 : 10;
  uint8_t fh[10];
  uint8_t body[MAX_TEXT];
  int found = 0;

  while (pos + hdrLen <= tagEnd && found < 3) {
    f.seek(pos);
    if (f.read(fh, hdrLen) != hdrLen || fh[0] == 0) break;     // padding reached
    uint32_t size;
    if (ver == 2)      size = fh[3] << 16 | fh[4] << 8 | fh[5];
    else if (ver == 4) size = syncsafe(fh + 4);
    else               size = fh[4] << 24 | fh[5] << 16 | fh[6] << 8 | fh[7];
    if (size == 0 || pos + hdrLen + size > tagEnd) break;

    String* target = nullptr;
    if (ver == 2) {
      if      (!memcmp(fh, "TT2", 3)) target = &out.title;
      else if (!memcmp(fh, "TP1", 3)) target = &out.artist;
      else if (!memcmp(fh, "TAL", 3)) target = &out.album;
    } else {
      if      (!memcmp(fh, "TIT2", 4)) target = &out.title;
      else if (!memcmp(fh, "TPE1", 4)) target = &out.artist;
      else if (!memcmp(fh, "TALB", 4)) target = &out.album;
    }
    // Compressed / encrypted frames (v2.3 flags) cannot be read as text.
    const bool unreadable = ver == 3 && (fh[9] & 0xC0);
    if (target && !unreadable && target->isEmpty()) {
      const size_t n = min<size_t>(size, MAX_TEXT);
      if (f.read(body, n) == n) {
        size_t len = n;
        if (unsync) {                          // undo 0xFF 0x00 -> 0xFF
          size_t w = 0;
          for (size_t r = 0; r < n; r++) {
            body[w++] = body[r];
            if (body[r] == 0xFF && r + 1 < n && body[r + 1] == 0x00) r++;
          }
          len = w;
        }
        *target = decodeText(body, len);
        if (target->length()) found++;
      }
    }
    pos += hdrLen + size;
  }
  return out.title.length() || out.artist.length() || out.album.length();
}
