// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "bt.h"
#include "audio.h"
#include "settings.h"
#include "BluetoothA2DPSource.h"
#include <esp_attr.h>

static BluetoothA2DPSource a2dp;

static BtDevice     devs[BT_MAX_DEVICES];
static int          devCount = 0;
static volatile uint32_t listSerial = 0;
static portMUX_TYPE devMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool haveTarget = false;
static uint8_t       targetAddr[6];
static char          targetName[33];

static volatile esp_a2d_connection_state_t state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;

// Survives ESP.restart() (not a power cycle): the request to boot into pairing.
static const uint32_t PAIRING_MAGIC = 0xB7A1B7A1;
RTC_NOINIT_ATTR static uint32_t rtcPairing;
static bool pairingBoot = false;
static volatile bool saveWanted = false;
static volatile bool adoptTarget = false;

// Bluetooth task: every device discovery reports.
static bool onSsid(const char* ssid, esp_bd_addr_t address, int rssi) {
  const char* name = (ssid && *ssid) ? ssid : "(no name)";
  portENTER_CRITICAL(&devMux);
  int i = 0;
  for (; i < devCount; i++)
    if (memcmp(devs[i].addr, address, 6) == 0) break;
  if (i == devCount && devCount < BT_MAX_DEVICES) devCount++;
  if (i < devCount) {
    strlcpy(devs[i].name, name, sizeof(devs[i].name));
    memcpy(devs[i].addr, address, 6);
    devs[i].rssi = rssi;
    listSerial++;
  }
  const bool tapped = haveTarget && memcmp(targetAddr, address, 6) == 0;
  portEXIT_CRITICAL(&devMux);

  // Outside a pairing boot, the saved speaker is welcome whenever it shows up.
  static const uint8_t NONE[6] = { 0 };
  const bool saved = !pairingBoot && memcmp(g_settings.btAddr, NONE, 6) != 0 &&
                     memcmp(g_settings.btAddr, address, 6) == 0;
  if (tapped || saved)
    Serial.printf("bt: found \"%s\" -- connecting\n", name);
  return tapped || saved;
}

static void onState(esp_a2d_connection_state_t s, void*) {
  static const char* NAMES[] = { "disconnected", "connecting", "connected", "disconnecting" };
  Serial.printf("bt: %s  heap %u\n", s <= 3 ? NAMES[s] : "?", ESP.getFreeHeap());
  state = s;
  if (s == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    esp_bd_addr_t* peer = a2dp.get_last_peer_address();
    bool changed = memcmp(g_settings.btAddr, *peer, 6) != 0;
    if (haveTarget) {                        // the String is assigned in bt_tick,
      adoptTarget = true;                    // on the UI's own task
      haveTarget = false;
      changed = true;
    }
    if (changed) {
      memcpy(g_settings.btAddr, *peer, 6);
      saveWanted = true;                     // NVS write happens in bt_tick, not
    }                                        // on this 3 KB Bluetooth task stack
  }
}

// ---------------------------------------------------------------------------
// Speaker buttons (AVRCP pass-through)
// ---------------------------------------------------------------------------
// The speaker's own play / next / previous buttons arrive here on the
// Bluetooth task. The audio_* calls belong to the loop task (they share the
// play order), so the key is only recorded; bt_tick() acts on it.
static const uint8_t AVRC_VOL_UP = 0x41, AVRC_VOL_DOWN = 0x42, AVRC_PLAY = 0x44,
                     AVRC_STOP = 0x45, AVRC_PAUSE = 0x46, AVRC_FORWARD = 0x4B,
                     AVRC_BACKWARD = 0x4C;
static volatile uint8_t pendingKey = 0;

static void onSpeakerKey(uint8_t key, bool isReleased) {
  if (isReleased) pendingKey = key;          // one action per press
}

static void handleSpeakerKey(uint8_t key) {
  switch (key) {
    case AVRC_PLAY:
      if (!audio_isPlaying()) audio_togglePause();
      break;
    case AVRC_PAUSE:
    case AVRC_STOP:
      if (audio_isPlaying()) audio_togglePause();
      break;
    case AVRC_FORWARD:  audio_next(); break;
    case AVRC_BACKWARD: audio_prev(); break;
    case AVRC_VOL_UP:   audio_setVolume(min(100, audio_volume() + 5)); break;
    case AVRC_VOL_DOWN: audio_setVolume(max(0, audio_volume() - 5)); break;
    default:
      Serial.printf("bt: speaker key 0x%02x ignored\n", key);
      return;
  }
  Serial.printf("bt: speaker key 0x%02x\n", key);
}

// Frame is {int16_t channel1, channel2}, packed: the interleaved layout
// audio_pull fills.
static int32_t onData(Frame* frames, int32_t count) {
  audio_pull((int16_t*)frames, count);
  return count;
}

void bt_begin() {
  pairingBoot = (rtcPairing == PAIRING_MAGIC);
  rtcPairing = 0;

  a2dp.set_local_name("CYD-MP3");
  a2dp.set_ssid_callback(onSsid);
  a2dp.set_on_connection_state_changed(onState);
  a2dp.set_data_callback_in_frames(onData);
  a2dp.set_avrc_passthru_command_callback(onSpeakerKey);
  a2dp.set_volume(100);                       // gain is applied in audio_pull

  // Normal boot: page the saved speaker first. Pairing boot: go straight to
  // discovery, then turn auto-reconnect back on so the speaker the user picks
  // is written to the library's NVS for next time.
  a2dp.set_auto_reconnect(!pairingBoot, 3);
  a2dp.start();
  if (pairingBoot) a2dp.set_auto_reconnect(true, 3);

  Serial.printf("bt: started (%s), saved speaker \"%s\"  heap %u\n",
                pairingBoot ? "PAIRING" : "reconnect", g_settings.btName.c_str(),
                ESP.getFreeHeap());
}

void bt_tick() {
  const uint8_t key = pendingKey;
  if (key) {
    pendingKey = 0;
    handleSpeakerKey(key);
  }

  if (saveWanted) {
    saveWanted = false;
    if (adoptTarget) { g_settings.btName = targetName; adoptTarget = false; }
    settings_save();
    Serial.printf("bt: saved speaker \"%s\"\n", g_settings.btName.c_str());
  }
}

bool   bt_connected()   { return state == ESP_A2D_CONNECTION_STATE_CONNECTED; }
bool   bt_connecting()  { return state == ESP_A2D_CONNECTION_STATE_CONNECTING; }
String bt_speakerName() { return g_settings.btName; }
bool   bt_pairingBoot() { return pairingBoot; }
bool   bt_choosing()    { return haveTarget; }
uint32_t bt_listSerial() { return listSerial; }

void bt_requestPairing() {
  Serial.println("bt: restarting into pairing");
  rtcPairing = PAIRING_MAGIC;
  delay(100);
  ESP.restart();
}

void bt_cancelPairing() {
  rtcPairing = 0;
  delay(100);
  ESP.restart();
}

int bt_deviceCount() {
  portENTER_CRITICAL(&devMux);
  const int n = devCount;
  portEXIT_CRITICAL(&devMux);
  return n;
}

bool bt_device(int i, BtDevice& out) {
  portENTER_CRITICAL(&devMux);
  const bool ok = i >= 0 && i < devCount;
  if (ok) out = devs[i];
  portEXIT_CRITICAL(&devMux);
  return ok;
}

void bt_choose(int i) {
  BtDevice d;
  if (!bt_device(i, d)) return;
  portENTER_CRITICAL(&devMux);
  memcpy(targetAddr, d.addr, 6);
  memcpy(targetName, d.name, sizeof(targetName));
  haveTarget = true;
  portEXIT_CRITICAL(&devMux);
  Serial.printf("bt: chose \"%s\" -- connecting when it is next seen\n", d.name);
}

#endif  // !CYD_UPLOADER
