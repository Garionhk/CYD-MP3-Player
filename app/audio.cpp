// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "audio.h"
#include "storage.h"
#include "settings.h"     // RepeatMode
#include "MP3DecoderHelix.h"

// ---------------------------------------------------------------------------
// Ring buffer: one producer (decode task), one consumer (audio_pull)
// ---------------------------------------------------------------------------
// 2048 stereo frames = 46 ms, 16 KB. 1024 was tried in stage 2 and underran
// ~80 times a second with a background playing. Not because Bluetooth asks
// for much at once -- it pulls 128 frames per call (audio_maxRequest()) -- but
// because the background's .anim reads share the SD card with this task, and
// 23 ms is not enough cushion to wait out them plus a frame decode. The same
// run showed background frames taking 300 ms instead of ~95.
static const uint32_t RB_FRAMES = 2048;
static int16_t*          rb = nullptr;
static volatile uint32_t rbHead = 0;          // producer only
static volatile uint32_t rbTail = 0;          // consumer only
static volatile bool     flushReq = false;    // producer asks, consumer does it

static inline uint32_t rbUsed() { return (rbHead - rbTail) & (RB_FRAMES - 1); }
static inline uint32_t rbFree() { return RB_FRAMES - 1 - rbUsed(); }

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static volatile int      reqTrack  = -1;      // UI -> decode task
static volatile int32_t  reqSeekMs = -1;
static volatile bool     playing   = true;
static volatile uint8_t  volumePct = 40;

static volatile int      curTrack  = 0;
static volatile uint32_t posBaseMs = 0;       // where in the track the ring started
static volatile uint32_t framesPlayed = 0;    // pulled since posBaseMs
static volatile uint32_t durationMs = 0;
static volatile uint32_t trackSerial = 0;
static volatile uint32_t underruns = 0;
static volatile bool     starting = false;    // opening a track: no underrun counting
static volatile uint32_t lastPullMs = 0;

static TaskHandle_t decodeHandle = nullptr;

// ---------------------------------------------------------------------------
// Consumer
// ---------------------------------------------------------------------------
static volatile int32_t maxRequest = 0;

void audio_pull(int16_t* out, int32_t frames) {
  lastPullMs = millis();
  if (frames > maxRequest) maxRequest = frames;
  if (flushReq) {
    rbTail = rbHead;
    framesPlayed = 0;
    flushReq = false;
  }
  int32_t i = 0;
  if (playing) {
    const int32_t gain = (int32_t)volumePct * volumePct;   // perceptual-ish, /10000
    uint32_t tail = rbTail;
    const int32_t n = min<int32_t>(frames, (rbHead - tail) & (RB_FRAMES - 1));
    for (; i < n; i++) {
      out[i * 2]     = (int16_t)((int32_t)rb[tail * 2]     * gain / 10000);
      out[i * 2 + 1] = (int16_t)((int32_t)rb[tail * 2 + 1] * gain / 10000);
      tail = (tail + 1) & (RB_FRAMES - 1);
    }
    rbTail = tail;
    framesPlayed += n;
    if (n > 0) starting = false;
    if (n < frames && !starting && reqTrack < 0 && reqSeekMs < 0) underruns++;
  }
  memset(out + i * 2, 0, (frames - i) * 2 * sizeof(int16_t));
}

// ---------------------------------------------------------------------------
// Producer: resampler and PCM callback
// ---------------------------------------------------------------------------
static volatile bool abortDecode = false;     // a request arrived mid-buffer
static uint32_t rsPos = 0;                    // 16.16 phase
static int16_t  rsPrevL = 0, rsPrevR = 0;

static inline bool requestPending() { return reqTrack >= 0 || reqSeekMs >= 0; }

static void pushFrame(int16_t l, int16_t r) {
  while (rbFree() == 0) {
    if (requestPending()) { abortDecode = true; return; }
    vTaskDelay(pdMS_TO_TICKS(4));             // Bluetooth is the clock
  }
  const uint32_t h = rbHead;
  rb[h * 2] = l;
  rb[h * 2 + 1] = r;
  rbHead = (h + 1) & (RB_FRAMES - 1);
}

static void onPcm(MP3FrameInfo& info, short* pcm, size_t len, void*) {
  if (abortDecode) return;
  const int ch = info.nChans;
  const size_t frames = len / ch;
  if (info.samprate == 44100) {
    for (size_t i = 0; i < frames && !abortDecode; i++) {
      const int16_t l = pcm[i * ch];
      pushFrame(l, ch == 2 ? pcm[i * ch + 1] : l);
    }
    return;
  }
  // Linear interpolation to 44.1 kHz for 48 k / 32 k / 22.05 k files.
  const uint32_t step = (uint32_t)(((uint64_t)info.samprate << 16) / 44100);
  for (size_t i = 0; i < frames && !abortDecode; i++) {
    const int16_t l = pcm[i * ch];
    const int16_t r = ch == 2 ? pcm[i * ch + 1] : l;
    while (rsPos < 65536 && !abortDecode) {
      const int32_t f = rsPos;
      pushFrame(rsPrevL + (((int32_t)l - rsPrevL) * f >> 16),
                rsPrevR + (((int32_t)r - rsPrevR) * f >> 16));
      rsPos += step;
    }
    rsPos -= 65536;
    rsPrevL = l; rsPrevR = r;
  }
}

static libhelix::MP3DecoderHelix mp3(onPcm);

// ---------------------------------------------------------------------------
// Track opening: ID3 skip, duration, seek
// ---------------------------------------------------------------------------
static File     file;
static uint32_t dataStart = 0;                // first byte after ID3v2
static uint32_t bitrateKbps = 0;              // of the first frame (for CBR maths)

static uint32_t id3v2Size(File& f) {
  uint8_t h[10];
  f.seek(0);
  if (f.read(h, 10) != 10 || memcmp(h, "ID3", 3) != 0) return 0;
  uint32_t size = ((h[6] & 0x7F) << 21) | ((h[7] & 0x7F) << 14) |
                  ((h[8] & 0x7F) << 7) | (h[9] & 0x7F);
  return size + 10 + ((h[5] & 0x10) ? 10 : 0);
}

struct FrameHdr { int version; int sampleRate; int bitrate; int spf; bool mono; int length; };

// Layer III only -- which is what ".mp3" means in practice.
static bool parseHeader(const uint8_t* p, FrameHdr& h) {
  if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return false;
  const int ver = (p[1] >> 3) & 3;            // 0 = 2.5, 2 = 2, 3 = 1
  if (ver == 1 || ((p[1] >> 1) & 3) != 1) return false;
  const int bri = p[2] >> 4, sri = (p[2] >> 2) & 3;
  if (bri == 0 || bri == 15 || sri == 3) return false;
  static const uint16_t BR_V1[15] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
  static const uint16_t BR_V2[15] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 };
  static const uint32_t SR[3][3] = { { 11025, 12000, 8000 }, { 0, 0, 0 },
                                     { 22050, 24000, 16000 } };
  static const uint32_t SR_V1[3] = { 44100, 48000, 32000 };
  h.version    = ver;
  h.bitrate    = (ver == 3) ? BR_V1[bri] : BR_V2[bri];
  h.sampleRate = (ver == 3) ? SR_V1[sri] : SR[ver][sri];
  h.spf        = (ver == 3) ? 1152 : 576;
  h.mono       = ((p[3] >> 6) & 3) == 3;
  h.length     = (ver == 3 ? 144000 : 72000) * h.bitrate / h.sampleRate + ((p[2] >> 1) & 1);
  return h.length > 4;
}

// Duration from the Xing/Info or VBRI header when there is one (exact, and the
// only right answer for VBR), else from the bitrate and the file size.
static uint32_t computeDuration(File& f) {
  static uint8_t buf[4096];
  f.seek(dataStart);
  const int n = f.read(buf, sizeof(buf));
  for (int i = 0; i + 4 <= n; i++) {
    FrameHdr h;
    if (!parseHeader(buf + i, h)) continue;
    // A real frame is followed by another one; a false sync (common inside
    // album art or junk) almost never is.
    FrameHdr next;
    if (i + h.length + 4 <= n && !parseHeader(buf + i + h.length, next)) continue;

    bitrateKbps = h.bitrate;
    const int xingOff = i + ((h.version == 3) ? (h.mono ? 21 : 36) : (h.mono ? 13 : 21));
    if (xingOff + 12 <= n &&
        (memcmp(buf + xingOff, "Xing", 4) == 0 || memcmp(buf + xingOff, "Info", 4) == 0)) {
      const uint32_t flags = (buf[xingOff + 4] << 24) | (buf[xingOff + 5] << 16) |
                             (buf[xingOff + 6] << 8) | buf[xingOff + 7];
      if (flags & 1) {
        const uint32_t frames = (buf[xingOff + 8] << 24) | (buf[xingOff + 9] << 16) |
                                (buf[xingOff + 10] << 8) | buf[xingOff + 11];
        return (uint32_t)((uint64_t)frames * h.spf * 1000 / h.sampleRate);
      }
    }
    const int vbriOff = i + 36;
    if (vbriOff + 18 <= n && memcmp(buf + vbriOff, "VBRI", 4) == 0) {
      const uint32_t frames = (buf[vbriOff + 14] << 24) | (buf[vbriOff + 15] << 16) |
                              (buf[vbriOff + 16] << 8) | buf[vbriOff + 17];
      return (uint32_t)((uint64_t)frames * h.spf * 1000 / h.sampleRate);
    }
    return (uint32_t)((uint64_t)(f.size() - dataStart - i) * 8 / h.bitrate);
  }
  return 0;
}

static void requestFlush() {
  // If Bluetooth is not pulling (not connected), nothing else touches rbTail.
  if (millis() - lastPullMs < 200) {
    flushReq = true;
    for (int i = 0; i < 50 && flushReq; i++) vTaskDelay(pdMS_TO_TICKS(4));
  }
  if (flushReq || millis() - lastPullMs >= 200) {
    rbTail = rbHead;
    framesPlayed = 0;
    flushReq = false;
  }
}

static void resetDecoder() {
  mp3.end();
  mp3.begin();
  rsPos = 0;
  rsPrevL = rsPrevR = 0;
}

// Byte offset for a time. Proportional over the audio data: exact for CBR,
// close for VBR (the Xing seek table is a later refinement).
static uint32_t byteForMs(uint32_t ms) {
  const uint32_t size = file.size();
  if (durationMs > 0)
    return dataStart + (uint32_t)((uint64_t)(size - dataStart) * ms / durationMs);
  if (bitrateKbps > 0)
    return min<uint32_t>(size, dataStart + (uint32_t)((uint64_t)ms * bitrateKbps / 8));
  return dataStart;
}

static bool openTrack(int index, uint32_t startMs) {
  const int count = storage_trackCount();
  if (count == 0) return false;
  index = ((index % count) + count) % count;

  starting = true;
  if (file) file.close();
  requestFlush();
  resetDecoder();

  curTrack = index;
  file = SD.open(storage_trackPath(index));
  if (!file) {
    Serial.printf("audio: cannot open %s\n", storage_trackPath(index).c_str());
    durationMs = 0;
    trackSerial++;
    return false;
  }
  dataStart = id3v2Size(file);
  bitrateKbps = 0;
  durationMs = computeDuration(file);
  if (startMs >= durationMs && durationMs > 0) startMs = 0;
  file.seek(startMs ? byteForMs(startMs) : dataStart);
  posBaseMs = startMs;
  framesPlayed = 0;
  trackSerial++;
  Serial.printf("audio: [%d/%d] %s  %u KB  %lu:%02lu  %u kbps  heap %u\n", index + 1, count,
                storage_trackPath(index).c_str(), (unsigned)(file.size() / 1024),
                durationMs / 60000, (durationMs / 1000) % 60, bitrateKbps, ESP.getFreeHeap());
  return true;
}

static void seekTo(uint32_t ms) {
  if (!file) return;
  if (durationMs > 1000) ms = min(ms, durationMs - 1000);
  starting = true;
  requestFlush();
  resetDecoder();                             // mid-stream bit reservoir is invalid now
  file.seek(byteForMs(ms));
  posBaseMs = ms;
  framesPlayed = 0;
}

// ---------------------------------------------------------------------------
// Decode task
// ---------------------------------------------------------------------------
// It opens what it is asked to open and reports when a track has run out; it
// never decides what plays next. Play order (shuffle, repeat) is the UI task's
// business in audio_tick(), so the order table needs no locking.
static volatile uint32_t reqStartMs  = 0;     // with reqTrack: where to start
static volatile uint32_t endedSerial = 0;     // bumped when a track finishes
static volatile uint32_t failSerial  = 0;     // bumped when a track will not open
static volatile uint32_t diagMaxReadMs = 0;   // diagnostics: slowest SD read

static void decodeTask(void*) {
  static uint8_t buf[1024];
  for (;;) {
    abortDecode = false;
    const int t = reqTrack;
    if (t >= 0) {
      const uint32_t startMs = reqStartMs;
      reqTrack = -1;
      reqStartMs = 0;
      reqSeekMs = -1;                          // a seek meant for the old track
      if (!openTrack(t, startMs)) failSerial++;
      continue;
    }
    const int32_t s = reqSeekMs;
    if (s >= 0) {
      reqSeekMs = -1;
      seekTo((uint32_t)s);
      continue;
    }
    if (!file) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    const uint32_t r0 = millis();
    const int n = file.read(buf, sizeof(buf));
    { const uint32_t took = millis() - r0; if (took > diagMaxReadMs) diagMaxReadMs = took; }
    if (n > 0) {
      mp3.write(buf, n);
      continue;
    }
    // End of track: let what is buffered play out, then say so.
    while (rbUsed() > 0 && !requestPending()) vTaskDelay(pdMS_TO_TICKS(10));
    if (!requestPending()) {
      file.close();
      endedSerial++;
    }
  }
}

// ---------------------------------------------------------------------------
// Play order (UI task only)
// ---------------------------------------------------------------------------
static uint16_t* order = nullptr;             // play position -> track index
static int       orderPos = 0;
static int       orderLen = 0;
static bool      shuffleOn = false;
static uint8_t   repeatMode = REPEAT_ALL;
static uint32_t  seenEnded = 0, seenFail = 0;
static int       failStreak = 0;

// Current track first, the rest in random order.
static void buildOrder(bool shuffled, int firstTrack) {
  for (int i = 0; i < orderLen; i++) order[i] = i;
  if (!shuffled || orderLen < 2) {
    orderPos = constrain(firstTrack, 0, orderLen - 1);
    return;
  }
  order[0] = firstTrack;
  order[firstTrack] = 0;
  for (int i = orderLen - 1; i > 1; i--) {
    const int j = 1 + esp_random() % i;
    const uint16_t tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }
  orderPos = 0;
}

static void request(int track, uint32_t startMs = 0) {
  reqStartMs = startMs;
  reqTrack = track;
}

static void advance(bool trackFinished) {
  if (orderLen == 0) return;
  if (trackFinished && repeatMode == REPEAT_ONE) {
    request(curTrack);
    return;
  }
  int next = orderPos + 1;
  if (next >= orderLen) {
    if (trackFinished && repeatMode == REPEAT_OFF) {
      // The end of the list: load the first track, paused, ready to go again.
      playing = false;
      next = 0;
    } else if (shuffleOn) {
      buildOrder(true, order[esp_random() % orderLen]);   // a fresh shuffle
      next = 0;
    } else {
      next = 0;
    }
  }
  orderPos = next;
  request(order[orderPos]);
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void audio_begin(int startTrack, uint32_t startMs, uint8_t volume, bool shuffle, uint8_t repeat) {
  rb = (int16_t*)malloc(RB_FRAMES * 2 * sizeof(int16_t));
  orderLen = storage_trackCount();
  order = (uint16_t*)malloc(max(1, orderLen) * sizeof(uint16_t));
  volumePct = min<uint8_t>(volume, 100);
  shuffleOn = shuffle;
  repeatMode = repeat % REPEAT_MODE_COUNT;
  if (orderLen > 0) {
    startTrack = constrain(startTrack, 0, orderLen - 1);
    buildOrder(shuffleOn, startTrack);
    request(startTrack, startMs);
  }
  // 5 KB: Helix plus this loop measured ~2 KB; opening a track now also reads
  // the library index, which uses a little more.
  xTaskCreatePinnedToCore(decodeTask, "mp3dec", 5120, nullptr, 5, &decodeHandle, 1);
}

void audio_tick() {
  if (endedSerial != seenEnded) {
    seenEnded = endedSerial;
    failStreak = 0;
    advance(true);
  }
  if (failSerial != seenFail) {
    seenFail = failSerial;
    // Skip unreadable files, but stop rather than spin if none will open.
    if (++failStreak < orderLen) advance(false);
    else playing = false;
  }
}

void audio_playTrack(int index) {
  if (orderLen == 0) return;
  index = constrain(index, 0, orderLen - 1);
  for (int i = 0; i < orderLen; i++)
    if (order[i] == index) { orderPos = i; break; }
  playing = true;
  failStreak = 0;
  request(index);
}

void audio_next() {
  failStreak = 0;
  advance(false);
}

void audio_prev() {
  if (orderLen == 0) return;
  failStreak = 0;
  if (audio_positionMs() > 3000) { request(curTrack); return; }
  orderPos = (orderPos + orderLen - 1) % orderLen;
  request(order[orderPos]);
}

void audio_togglePause()        { playing = !playing; }
void audio_setVolume(uint8_t p) { volumePct = min<uint8_t>(p, 100); }

void audio_seekRelative(int seconds) {
  int64_t target = (int64_t)audio_positionMs() + (int64_t)seconds * 1000;
  if (target < 0) target = 0;
  reqSeekMs = (int32_t)target;
}

void audio_setShuffle(bool on) {
  if (on == shuffleOn || orderLen == 0) return;
  shuffleOn = on;
  buildOrder(on, curTrack);
}

void audio_setRepeat(uint8_t mode) { repeatMode = mode % REPEAT_MODE_COUNT; }
bool    audio_shuffle()            { return shuffleOn; }
uint8_t audio_repeat()             { return repeatMode; }

bool     audio_isPlaying()    { return playing; }
int      audio_currentTrack() { return curTrack; }
uint8_t  audio_volume()       { return volumePct; }
uint32_t audio_durationMs()   { return durationMs; }
uint32_t audio_trackSerial()  { return trackSerial; }
uint32_t audio_underruns()    { return underruns; }
uint32_t audio_takeMaxReadMs() { const uint32_t v = diagMaxReadMs; diagMaxReadMs = 0; return v; }
int32_t  audio_maxRequest()   { return maxRequest; }
uint8_t  audio_bufferPct()    { return rbUsed() * 100 / RB_FRAMES; }
bool     audio_bufferLow()    { return playing && rbUsed() < RB_FRAMES / 2; }

uint32_t audio_positionMs() {
  const uint32_t ms = posBaseMs + (uint32_t)((uint64_t)framesPlayed * 1000 / 44100);
  return (durationMs && ms > durationMs) ? durationMs : ms;
}

uint32_t audio_decodeStackFree() {
  return decodeHandle ? uxTaskGetStackHighWaterMark(decodeHandle) : 0;
}

#endif  // !CYD_UPLOADER
