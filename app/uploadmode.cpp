// Uploader image only (firmware.h).
#ifdef CYD_UPLOADER

// WebServer.h first: TFT_eSPI defines FS_NO_GLOBALS before including FS.h,
// which hides the plain `FS` name WebServer.h is written against.
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SD.h>
#include "uploadmode.h"
#include "upload_page.h"
#include "settings.h"
#include "display.h"
#include "theme.h"
#include "touch.h"
#include "ui.h"
#include <esp_attr.h>
#include <vector>
#include <algorithm>

#include "firmware.h"

// Survives ESP.restart() within the uploader: "use the hotspot this time".
static const uint32_t MAGIC_HOTSPOT = 0x55504C41;
RTC_NOINIT_ATTR static uint32_t rtcMode;

// Created only when upload mode runs. As globals their constructors ran on
// every boot and took heap the player's Bluetooth link needs.
static WebServer* web = nullptr;
static DNSServer* dns = nullptr;
#define server (*web)
static bool      apMode = false;
static String    apName;
static uint32_t  restartAt = 0;
static bool      restartToPlayer = true;

// Card space. usedBytes() walks the FAT, so it is computed on demand and kept.
static uint64_t cardUsed = 0;
static bool     cardUsedValid = false;

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------
static String activity;                 // "Receiving name.mp3  2.1 MB"
static bool   activityDirty = true;

static Rect doneRect()    { return { 0, tft.height() - 50, apMode ? tft.width() : tft.width() / 2, 50 }; }
static Rect hotspotRect() { return { tft.width() / 2, tft.height() - 50, tft.width() / 2, 50 }; }

static void drawButton(const Rect& r, Txt label, bool primary) {
  ui_textButton(r, label, primary);
}

static void drawScreen() {
  const Palette& p = theme();
  const int W = tft.width(), cx = W / 2;
  tft.fillScreen(p.bg);
  tft.fillRect(0, 0, W, UI_HEADER_H, p.band);
  ui_label(T_UPLOAD_MODE, 10, UI_HEADER_H / 2, ML_DATUM, p.text, p.band);

  int y = UI_HEADER_H + 22;
  tft.setTextDatum(MC_DATUM);
  if (apMode) {
    ui_label(T_JOIN_WIFI, cx, y, MC_DATUM, p.dim, p.bg, W - 10);
    tft.setTextColor(p.text, p.bg);
    tft.drawString(apName, cx, y + 28, 4);
    ui_label(T_THEN_OPEN, cx, y + 62, MC_DATUM, p.dim, p.bg, W - 10);
    tft.setTextColor(p.accent, p.bg);
    tft.drawString("http://" + WiFi.softAPIP().toString(), cx, y + 90, W >= 300 ? 4 : 2);
  } else {
    ui_label(T_OPEN_BROWSER, cx, y + 10, MC_DATUM, p.dim, p.bg, W - 10);
    tft.setTextColor(p.accent, p.bg);
    tft.drawString("http://" + WiFi.localIP().toString(), cx, y + 42, W >= 300 ? 4 : 2);
    tft.setTextColor(p.dim, p.bg);
    tft.drawString("WiFi: " + WiFi.SSID(), cx, y + 74, 2);
  }
  drawButton(doneRect(), T_DONE, true);
  if (!apMode) drawButton(hotspotRect(), T_HOTSPOT, false);
  activityDirty = true;
}

static void drawActivity() {
  const Palette& p = theme();
  const int y = tft.height() - 72;
  tft.fillRect(0, y, tft.width(), 20, p.bg);
  if (activity.length())
    ui_text(activity, tft.width() / 2, y + 10, MC_DATUM, p.good, p.bg, tft.width() - 10);
  activityDirty = false;
}

static void centreMessage(Txt line, uint16_t colour) {
  tft.fillScreen(theme().bg);
  ui_label(line, tft.width() / 2, tft.height() / 2, MC_DATUM, colour, theme().bg, tft.width() - 10);
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
// Everything the page can touch is under /music or /bg (or the one font file).
// No "..", no hidden names, no backslashes: a request can never reach /.sys's
// converted frames, the library index, or anything outside the card's folders.
static bool safeUnder(const String& path, const char* root) {
  const size_t n = strlen(root);
  if (!path.startsWith(root)) return false;
  if (path.length() > n && path[n] != '/') return false;
  if (path.indexOf("..") >= 0 || path.indexOf('\\') >= 0 || path.indexOf("//") >= 0) return false;
  if (path.indexOf("/.", n) >= 0) return false;
  for (unsigned i = 0; i < path.length(); i++)
    if ((uint8_t)path[i] < 0x20) return false;
  return true;
}

static bool safeUserPath(const String& path) {
  return safeUnder(path, "/music") || safeUnder(path, "/bg");
}

// A file name from a browser: last path component, no control characters,
// none of the characters FAT refuses.
static String cleanName(String name) {
  const int slash = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
  if (slash >= 0) name = name.substring(slash + 1);
  String out;
  for (unsigned i = 0; i < name.length(); i++) {
    const char c = name[i];
    if ((uint8_t)c < 0x20 || strchr("<>:\"|?*", c)) continue;
    out += c;
  }
  out.trim();
  while (out.startsWith(".")) out.remove(0, 1);
  return out;
}

static bool endsWithCI(const String& s, const char* suffix) {
  String low = s;
  low.toLowerCase();
  return low.endsWith(suffix);
}

static void sendJson(int code, const String& body) {
  server.send(code, "application/json", body);
}

static String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (unsigned i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

static void sendError(const String& message) {
  sendJson(400, "{\"ok\":false,\"error\":\"" + jsonEscape(message) + "\"}");
}

static void sendOk(const String& extra = String()) {
  sendJson(200, "{\"ok\":true" + extra + "}");
}

// ---------------------------------------------------------------------------
// Backgrounds are numbered bg1..bgN with no gaps (background.cpp stops at the
// first missing number), so deleting one renumbers the rest.
// ---------------------------------------------------------------------------
static int bgCount() {
  int n = 0;
  char p[24];
  for (int i = 1; i < 100; i++) {
    snprintf(p, sizeof(p), "/bg/bg%d.gif", i);
    if (!SD.exists(p)) break;
    n = i;
  }
  return n;
}

static void deleteBackground(int k) {
  const int count = bgCount();
  char from[24], to[24];
  snprintf(to, sizeof(to), "/bg/bg%d.gif", k);
  SD.remove(to);
  for (int i = k + 1; i <= count; i++) {
    snprintf(from, sizeof(from), "/bg/bg%d.gif", i);
    snprintf(to, sizeof(to), "/bg/bg%d.gif", i - 1);
    SD.rename(from, to);
  }
  // Every converted frame file from k on now belongs to a different GIF.
  for (int i = k; i <= count; i++) {
    snprintf(to, sizeof(to), "/.sys/bg%d.anim", i);
    SD.remove(to);
  }
}

// A new font changes how every title strip looks: drop them all, the player
// re-renders on its next boot.
static void forgetTitleStrips() {
  File d = SD.open("/.sys/t");
  if (!d) return;
  std::vector<String> names;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    names.push_back(String("/.sys/t/") + f.name());
    f.close();
  }
  d.close();
  for (const String& n : names) SD.remove(n);
}

static bool removeRecursive(const String& path) {
  File f = SD.open(path);
  if (!f) return false;
  if (!f.isDirectory()) {
    f.close();
    return SD.remove(path);
  }
  std::vector<String> children;
  for (File c = f.openNextFile(); c; c = f.openNextFile()) {
    children.push_back(path + "/" + c.name());
    c.close();
  }
  f.close();
  for (const String& c : children) removeRecursive(c);
  return SD.rmdir(path);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
static void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent(g_settings.lang == LANG_ZH ? "<script>var Z=true</script>"
                                                : "<script>var Z=false</script>");
  server.sendContent_P(UPLOAD_PAGE);
  server.sendContent("");
}

static void handleInfo() {
  if (!cardUsedValid) {
    cardUsed = SD.usedBytes();
    cardUsedValid = true;
  }
  const uint64_t total = SD.totalBytes();
  String j = "{\"total\":" + String((double)total, 0) +
             ",\"free\":" + String((double)(total > cardUsed ? total - cardUsed : 0), 0) +
             ",\"ap\":" + (apMode ? "true" : "false") +
             ",\"ssid\":\"" + jsonEscape(apMode ? apName : WiFi.SSID()) + "\"" +
             ",\"ip\":\"" + (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\"" +
             ",\"saved\":\"" + jsonEscape(g_settings.wifiSsid) + "\"}";
  sendJson(200, j);
}

static void handleList() {
  const String dir = server.arg("dir");
  if (!safeUserPath(dir)) { sendError("bad folder"); return; }
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) { sendError("no such folder"); return; }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"items\":[");
  bool first = true;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    const String name = f.name();
    if (!name.startsWith(".") && !name.endsWith(".part")) {
      String item = first ? "" : ",";
      item += "{\"name\":\"" + jsonEscape(name) + "\",\"dir\":" + (f.isDirectory() ? "true" : "false") +
              ",\"size\":" + String((unsigned long)f.size()) + "}";
      server.sendContent(item);
      first = false;
    }
    f.close();
  }
  d.close();
  server.sendContent("]}");
  server.sendContent("");
}

static void handleFile() {
  const String path = server.arg("path");
  if (!safeUnder(path, "/bg")) { server.send(404, "text/plain", "not found"); return; }
  File f = SD.open(path);
  if (!f || f.isDirectory()) { server.send(404, "text/plain", "not found"); return; }
  server.streamFile(f, "image/gif");
  f.close();
}

static void handleFont() {
  File f = SD.open("/.sys/cjk16.vlw");
  const unsigned long size = f ? f.size() : 0;
  if (f) f.close();
  sendJson(200, "{\"size\":" + String(size) + "}");
}

static void handleScan() {
  const int n = WiFi.scanNetworks();
  String j = "[";
  std::vector<String> seen;
  for (int i = 0; i < n; i++) {
    const String s = WiFi.SSID(i);
    if (s.isEmpty() || std::find(seen.begin(), seen.end(), s) != seen.end()) continue;
    seen.push_back(s);
    j += (seen.size() > 1 ? ",\"" : "\"") + jsonEscape(s) + "\"";
  }
  j += "]";
  WiFi.scanDelete();
  sendJson(200, j);
}

static void handleWifi() {
  g_settings.wifiSsid = server.arg("ssid");
  g_settings.wifiSsid.trim();
  // An empty password field keeps the saved one when the network is the same,
  // so re-saving the form cannot wipe a password the page never shows.
  const String pass = server.arg("pass");
  if (g_settings.wifiSsid.isEmpty()) g_settings.wifiPass = "";
  else if (pass.length()) g_settings.wifiPass = pass;
  settings_save();
  sendOk();
  Serial.printf("upload: wifi set to \"%s\" -- restarting\n", g_settings.wifiSsid.c_str());
  restartToPlayer = false;                 // come back up in upload mode, on the new network
  restartAt = millis() + 1000;             // let the response go out first
}

static void handleDelete() {
  const String path = server.arg("path");
  if (!safeUserPath(path) || path == "/music" || path == "/bg") { sendError("cannot delete that"); return; }
  if (path.startsWith("/bg/")) {
    int k = 0;
    if (sscanf(path.c_str(), "/bg/bg%d.gif", &k) == 1 && k > 0) deleteBackground(k);
    else SD.remove(path);
  } else if (!removeRecursive(path)) {
    sendError("delete failed");
    return;
  }
  cardUsedValid = false;
  Serial.printf("upload: deleted %s\n", path.c_str());
  sendOk();
}

static void handleRename() {
  const String path = server.arg("path");
  String name = cleanName(server.arg("name"));
  if (!safeUnder(path, "/music") || path == "/music" || name.isEmpty()) { sendError("bad name"); return; }
  File f = SD.open(path);
  if (!f) { sendError("not found"); return; }
  const bool isDir = f.isDirectory();
  f.close();
  // A song renamed without ".mp3" would silently vanish from the library.
  if (!isDir && !endsWithCI(name, ".mp3")) name += ".mp3";
  const String target = path.substring(0, path.lastIndexOf('/') + 1) + name;
  if (!safeUnder(target, "/music")) { sendError("bad name"); return; }
  if (SD.exists(target)) { sendError("that name is taken"); return; }
  if (!SD.rename(path, target)) { sendError("rename failed"); return; }
  sendOk();
}

static void handleMkdir() {
  const String path = server.arg("path");
  // The library scans two folder levels below /music.
  int depth = 0;
  for (unsigned i = strlen("/music"); i < path.length(); i++) if (path[i] == '/') depth++;
  if (!safeUnder(path, "/music") || path == "/music" || depth > 2) { sendError("bad folder name"); return; }
  if (!SD.mkdir(path)) { sendError("could not create folder"); return; }
  sendOk();
}

static void handleDone() {
  sendOk();
  restartToPlayer = true;
  restartAt = millis() + 800;
}

// ---- upload ---------------------------------------------------------------
static File     upFile;
static String   upTmp, upFinal, upError;
static uint32_t upLastDraw = 0;

static void finishUploadFail(const String& why) {
  if (upFile) upFile.close();
  if (upTmp.length()) SD.remove(upTmp);
  if (upError.isEmpty()) upError = why;
}

static void handleUploadData() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    upError = "";
    upTmp = upFinal = "";
    const String dir = server.arg("dir");
    const String name = cleanName(up.filename);
    if (dir == "/.sys") {
      if (!endsWithCI(name, ".vlw")) { upError = "the font must be a .vlw file"; return; }
      upFinal = "/.sys/cjk16.vlw";
    } else if (dir == "/bg") {
      if (!endsWithCI(name, ".gif")) { upError = "not a .gif"; return; }
      upFinal = "/bg/bg" + String(bgCount() + 1) + ".gif";
    } else if (safeUnder(dir, "/music")) {
      if (!endsWithCI(name, ".mp3")) { upError = "not an .mp3"; return; }
      if (name.isEmpty()) { upError = "bad file name"; return; }
      upFinal = dir + "/" + name;
    } else {
      upError = "bad folder";
      return;
    }
    // Written under a temporary name and renamed at the end, so an interrupted
    // upload never leaves half a song in the library.
    upTmp = upFinal + ".part";
    SD.remove(upTmp);
    upFile = SD.open(upTmp, FILE_WRITE);
    if (!upFile) { upError = "cannot write to the card"; return; }
    activity = String(tr(T_RECEIVING)) + " " + name;
    activityDirty = true;
    Serial.printf("upload: %s -> %s\n", name.c_str(), upFinal.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!upFile) return;
    if (upFile.write(up.buf, up.currentSize) != up.currentSize) {
      finishUploadFail("card full or write error");
      return;
    }
    if (millis() - upLastDraw > 700) {
      upLastDraw = millis();
      const int sp = activity.lastIndexOf("  ");
      if (sp > 0) activity = activity.substring(0, sp);
      activity += "  " + String(up.totalSize / 1024) + " KB";
      drawActivity();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!upFile) return;
    upFile.close();
    if (upFinal.startsWith("/bg/")) {
      File g = SD.open(upTmp);
      uint8_t h[10] = { 0 };
      const bool read = g && g.read(h, 10) == 10;
      if (g) g.close();
      const int w = h[6] | (h[7] << 8);
      if (!read || memcmp(h, "GIF", 3) != 0) { finishUploadFail("not a GIF"); return; }
      if (w > 2048) { finishUploadFail("GIF wider than 2048 px"); return; }
    }
    SD.remove(upFinal);
    if (!SD.rename(upTmp, upFinal)) { finishUploadFail("could not save the file"); return; }
    if (upFinal == "/.sys/cjk16.vlw") forgetTitleStrips();
    cardUsedValid = false;
    activity = "OK  " + upFinal.substring(upFinal.lastIndexOf('/') + 1);
    activityDirty = true;
    Serial.printf("upload: saved %s (%u KB)\n", upFinal.c_str(), (unsigned)(up.totalSize / 1024));
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    finishUploadFail("upload interrupted");
    activity = "";
    activityDirty = true;
  }
}

static void handleUploadDone() {
  if (upError.length()) sendError(upError);
  else sendOk(",\"name\":\"" + jsonEscape(upFinal.substring(upFinal.lastIndexOf('/') + 1)) + "\"");
}

static void handleNotFound() {
  // Captive portal: phones probe a known URL when joining a network; answering
  // with a redirect to this page makes them open it without being asked.
  if (apMode) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "not found");
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
// To the player, or back into this uploader (optionally forcing the hotspot).
static void restartNow(bool toPlayer, bool hotspot = false) {
  centreMessage(T_RESTARTING, theme().text);
  delay(300);
  if (toPlayer) firmware_bootPlayer();     // restarts
  rtcMode = hotspot ? MAGIC_HOTSPOT : 0;
  // Boot was pointed at the player when this image started; point it back here.
  if (!firmware_bootUploaderAgain()) firmware_bootPlayer();
}

static bool joinSavedNetwork() {
  if (g_settings.wifiSsid.isEmpty()) return false;
  centreMessage(T_CONNECTING_WIFI, theme().text);
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_settings.wifiSsid.c_str(), g_settings.wifiPass.c_str());
  Serial.printf("upload: joining \"%s\"", g_settings.wifiSsid.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" ok  http://%s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println(" failed");
  centreMessage(T_WIFI_FAILED, theme().warn);
  delay(2000);
  return false;
}

static void startHotspot() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[24];
  snprintf(name, sizeof(name), "CYD-MP3-%02X%02X", mac[4], mac[5]);
  apName = name;
  WiFi.disconnect(true);
  // AP + station: the station half is what lets the WiFi tab scan for networks.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName.c_str());           // open: nothing secret is served here
  dns = new DNSServer();
  dns->setErrorReplyCode(DNSReplyCode::NoError);
  dns->start(53, "*", WiFi.softAPIP());
  apMode = true;
  Serial.printf("upload: hotspot \"%s\" -> http://%s\n", apName.c_str(),
                WiFi.softAPIP().toString().c_str());
}

void uploadmode_run() {
  const bool forceHotspot = (rtcMode == MAGIC_HOTSPOT);
  rtcMode = 0;
  // From here on any reset -- a crash, a power cut, the reset button -- comes
  // back up as the player. Only a deliberate restart below returns here.
  firmware_nextBootPlayer();
  Serial.printf("upload: mode starting, heap %u\n", ESP.getFreeHeap());

  if (forceHotspot || !joinSavedNetwork()) startHotspot();
  web = new WebServer(80);

  server.on("/",            HTTP_GET,  handleRoot);
  server.on("/api/info",    HTTP_GET,  handleInfo);
  server.on("/api/list",    HTTP_GET,  handleList);
  server.on("/api/font",    HTTP_GET,  handleFont);
  server.on("/api/scan",    HTTP_GET,  handleScan);
  server.on("/file",        HTTP_GET,  handleFile);
  server.on("/api/wifi",    HTTP_POST, handleWifi);
  server.on("/api/delete",  HTTP_POST, handleDelete);
  server.on("/api/rename",  HTTP_POST, handleRename);
  server.on("/api/mkdir",   HTTP_POST, handleMkdir);
  server.on("/api/done",    HTTP_POST, handleDone);
  server.on("/api/upload",  HTTP_POST, handleUploadDone, handleUploadData);
  server.onNotFound(handleNotFound);
  server.begin();

  drawScreen();
  for (;;) {
    if (dns) dns->processNextRequest();
    server.handleClient();

    int x, y;
    if (touch_poll(&x, &y) == TOUCH_TAP) {
      if (doneRect().contains(x, y)) restartNow(true);
      else if (!apMode && hotspotRect().contains(x, y)) restartNow(false, true);
    }
    if (activityDirty) drawActivity();
    if (restartAt && millis() >= restartAt) restartNow(restartToPlayer);
    delay(2);
  }
}

#endif  // CYD_UPLOADER
