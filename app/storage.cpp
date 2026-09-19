#include "storage.h"
#include "board.h"
#include "id3.h"
#include <SPI.h>
#include <vector>
#include <algorithm>
#include <esp_rom_crc.h>

static SPIClass sdSPI(VSPI);

static const char* INDEX_PATH = "/.sys/library.idx";
static const char* INDEX_TMP  = "/.sys/library.tmp";
static const char* INDEX_MAGIC = "CYDLIB2";
static const int   MAX_TRACKS = 2000;

static uint32_t* offsets = nullptr;
static int       trackCount = 0;
static uint32_t  libraryCrc = 0;

static SemaphoreHandle_t mutex = nullptr;
static int    cacheIndex = -1;
static String cachePath, cacheLabel;

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------
struct Found { String path; uint32_t size; };

static void scanDir(const String& path, int depth, std::vector<Found>& out) {
  File d = SD.open(path);
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f && (int)out.size() < MAX_TRACKS; f = d.openNextFile()) {
    const char* name = f.name();
    if (name[0] != '.') {
      const String p = path + "/" + name;
      if (f.isDirectory()) {
        if (depth < 2) scanDir(p, depth + 1, out);
      } else {
        String low = p;
        low.toLowerCase();
        if (low.endsWith(".mp3")) out.push_back({ p, (uint32_t)f.size() });
      }
    }
    f.close();
  }
  d.close();
}

static String fileBaseName(const String& path) {
  String name = path.substring(path.lastIndexOf('/') + 1);
  const int dot = name.lastIndexOf('.');
  return dot > 0 ? name.substring(0, dot) : name;
}

static String makeLabel(const String& path, const String& title, const String& artist) {
  String label = title.length() ? title : fileBaseName(path);
  if (artist.length()) label += " - " + artist;
  return label;
}

// Split "path\ttitle\tartist" into its parts.
static void splitLine(const String& line, String& path, String& title, String& artist) {
  const int a = line.indexOf('\t');
  const int b = (a < 0) ? -1 : line.indexOf('\t', a + 1);
  path   = (a < 0) ? line : line.substring(0, a);
  title  = (a < 0) ? String() : line.substring(a + 1, b < 0 ? line.length() : b);
  artist = (b < 0) ? String() : line.substring(b + 1);
}

// ---------------------------------------------------------------------------
// Index file
// ---------------------------------------------------------------------------
static bool indexIsCurrent(int count, uint32_t crc) {
  File f = SD.open(INDEX_PATH);
  if (!f) return false;
  const String header = f.readStringUntil('\n');
  f.close();
  char expect[40];
  snprintf(expect, sizeof(expect), "%s %d %08x", INDEX_MAGIC, count, crc);
  return header == expect;
}

static bool buildIndex(const std::vector<Found>& tracks, uint32_t crc, StorageProgressFn progress) {
  SD.remove(INDEX_TMP);
  File out = SD.open(INDEX_TMP, FILE_WRITE);
  if (!out) return false;
  out.printf("%s %d %08x\n", INDEX_MAGIC, (int)tracks.size(), crc);

  const uint32_t t0 = millis();
  int tagged = 0;
  for (size_t i = 0; i < tracks.size(); i++) {
    Id3Info tag;
    File f = SD.open(tracks[i].path);
    if (f) {
      if (id3_read(f, tag)) tagged++;
      f.close();
    }
    out.print(tracks[i].path);
    out.print('\t');
    out.print(tag.title);
    out.print('\t');
    out.print(tag.artist);
    out.print('\n');
    if (progress) progress(i + 1, tracks.size());
  }
  out.close();
  SD.remove(INDEX_PATH);
  SD.rename(INDEX_TMP, INDEX_PATH);
  Serial.printf("library: indexed %u tracks (%d with ID3 tags) in %lu ms\n",
                (unsigned)tracks.size(), tagged, millis() - t0);
  return true;
}

// One pass over the index: record where each line starts.
static bool loadOffsets(int expected) {
  free(offsets);
  offsets = (uint32_t*)malloc(max(1, expected) * sizeof(uint32_t));
  if (!offsets) return false;
  File f = SD.open(INDEX_PATH);
  if (!f) return false;

  static uint8_t buf[1024];
  uint32_t filePos = 0;
  bool headerDone = false;
  int n = 0;
  for (;;) {
    const int got = f.read(buf, sizeof(buf));
    if (got <= 0) break;
    for (int i = 0; i < got; i++) {
      if (buf[i] != '\n') continue;
      const uint32_t next = filePos + i + 1;
      if (!headerDone) headerDone = true;
      else n++;
      if (n < expected && next < f.size()) offsets[n] = next;
    }
    filePos += got;
  }
  f.close();
  // The first entry starts right after the header line.
  trackCount = min(n, expected);
  return trackCount == expected;
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
bool storage_mount() {
  if (!mutex) mutex = xSemaphoreCreateMutex();
  sdSPI.begin(CYD_SD_SCK_PIN, CYD_SD_MISO_PIN, CYD_SD_MOSI_PIN, CYD_SD_CS_PIN);
  // max_files 4: the playing track, the background, a title strip or index
  // read, one spare. Each costs heap only while actually open.
  bool ok = SD.begin(CYD_SD_CS_PIN, sdSPI, 20000000, "/sd", 4);
  if (!ok) {
    SD.end();
    ok = SD.begin(CYD_SD_CS_PIN, sdSPI, 4000000, "/sd", 4);
  }
  if (!ok) {
    Serial.println("sd: mount failed");
    return false;
  }
  for (const char* dir : { "/music", "/bg", "/.sys" })
    if (!SD.exists(dir)) SD.mkdir(dir);
  return true;
}

bool storage_begin(StorageProgressFn progress) {
  if (!storage_mount()) return false;

  // Walk /music. Runs before Bluetooth, so a temporary list is affordable.
  const uint32_t t0 = millis();
  std::vector<Found> tracks;
  scanDir("/music", 0, tracks);
  std::sort(tracks.begin(), tracks.end(),
            [](const Found& a, const Found& b) { return a.path < b.path; });
  uint32_t crc = 0;
  for (const Found& t : tracks) {
    crc = esp_rom_crc32_le(crc, (const uint8_t*)t.path.c_str(), t.path.length());
    crc = esp_rom_crc32_le(crc, (const uint8_t*)&t.size, sizeof(t.size));
  }
  Serial.printf("sd: %u MB card, %u tracks (scan %lu ms)\n",
                (unsigned)(SD.cardSize() / (1024ULL * 1024ULL)), (unsigned)tracks.size(),
                millis() - t0);

  if (!indexIsCurrent(tracks.size(), crc)) buildIndex(tracks, crc, progress);
  const int count = tracks.size();
  tracks.clear();
  tracks.shrink_to_fit();

  libraryCrc = crc;
  cacheIndex = -1;
  if (!loadOffsets(count)) {
    Serial.println("library: index unreadable -- no tracks");
    trackCount = 0;
  }
  return true;
}

int      storage_trackCount() { return trackCount; }
uint32_t storage_libraryCrc() { return libraryCrc; }

// Read entry i into the cache. Caller holds the mutex.
static bool fetch(int i) {
  if (i < 0 || i >= trackCount) return false;
  if (i == cacheIndex) return true;
  File f = SD.open(INDEX_PATH);
  if (!f) return false;
  f.seek(offsets[i]);
  const String line = f.readStringUntil('\n');
  f.close();
  String path, title, artist;
  splitLine(line, path, title, artist);
  cachePath = path;
  cacheLabel = makeLabel(path, title, artist);
  cacheIndex = i;
  return true;
}

String storage_trackPath(int i) {
  String out;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (fetch(i)) out = cachePath;
  xSemaphoreGive(mutex);
  return out;
}

String storage_trackLabel(int i) {
  String out;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (fetch(i)) out = cacheLabel;
  xSemaphoreGive(mutex);
  return out;
}

void storage_forEach(StorageEachFn fn) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  File f = SD.open(INDEX_PATH);
  if (f) {
    f.readStringUntil('\n');                   // header
    for (int i = 0; i < trackCount; i++) {
      const String line = f.readStringUntil('\n');
      String path, title, artist;
      splitLine(line, path, title, artist);
      fn(i, path, makeLabel(path, title, artist));
    }
    f.close();
  }
  xSemaphoreGive(mutex);
}
