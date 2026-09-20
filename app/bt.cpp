// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "bt.h"
#include "audio.h"
#include "settings.h"
#include "BluetoothA2DPSource.h"
#include <esp_attr.h>
#include <esp_gap_bt_api.h>

// ---------------------------------------------------------------------------
// The library, with two things it does not offer on its own
// ---------------------------------------------------------------------------
// 1. Connecting to a device the owner tapped, at the moment they tap it.
//    The library only ever connects from its own discovery: when the ssid
//    callback returns true it marks the device DISCOVERED and stops the
//    inquiry, and the "discovery stopped" handler is what calls
//    esp_a2d_connect. That works for a speaker, which answers every inquiry.
//    A car head unit announces itself once while its pairing screen is open
//    and then goes quiet, so waiting to see it a second time waits forever.
//    Driving the library's own path -- name it, mark it DISCOVERED, stop the
//    inquiry -- connects immediately and leaves its state machine in step.
//
// 2. Reporting what happened. Pairing results reach ESP_LOG only, which an
//    Arduino build compiles out, so a failure was invisible and the screen
//    just said "connecting" forever.
class CydSource : public BluetoothA2DPSource {
 public:
  bool connectNow(const uint8_t addr[6], const char* name) {
    memcpy(peer_bd_addr, addr, ESP_BD_ADDR_LEN);
    strlcpy((char*)s_peer_bdname, name, sizeof(s_peer_bdname));
    s_a2d_state = APP_AV_STATE_DISCOVERED;
    if (discovery_active && esp_bt_gap_cancel_discovery() == ESP_OK) {
      Serial.println("bt: inquiry cancelled -- connecting from its stop");
      return true;                       // the stop handler connects
    }
    s_a2d_state = APP_AV_STATE_CONNECTING;
    const esp_err_t err = esp_a2d_connect((uint8_t*)addr);
    Serial.printf("bt: esp_a2d_connect -> %d (%s)\n", err, esp_err_to_name(err));
    return err == ESP_OK;
  }

  // Who is actually on the other end. In the source role the library records
  // the peer only when its OWN discovery accepts one (filter_inquiry_scan_result
  // -> set_last_connection); a connection made any other way -- tapped here, or
  // opened by the car itself -- leaves get_last_peer_address() pointing at
  // whatever connected last time. That is how the car's audio came out of the
  // car while the screen still named the speaker, and why the next boot tried
  // the speaker again. Take the address from the connection event instead, and
  // tell the library, so its own reconnect targets the right device too.
  const uint8_t* peer() const { return lastPeer; }

  // Chasing the last device on every disconnect is the library's idea, not
  // ours: handle_reconnect_logic() pages last_connection up to three times
  // whenever a link drops. That is right at start-up and wrong afterwards --
  // asked to move from one speaker to another, it pages the one just left and
  // wins the race, so the new one answers nothing. The flag is re-derived in
  // start(), so turning it off costs nothing at the next boot.
  void chaseLastDevice(bool on) { is_autoreconnect_allowed = on; }

  // Drop a named link. The library's own disconnect() uses last_connection,
  // which is no use when that record is the thing being erased.
  void disconnectFrom(const uint8_t addr[6]) { esp_a2d_disconnect((uint8_t*)addr); }

  // Back to looking, after a failure or a give-up.
  void rediscover() {
    s_a2d_state = APP_AV_STATE_DISCOVERING;
    if (!discovery_active) esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
  }

 protected:
  void app_a2d_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) override {
    // Before delegating: the base calls our connection-state callback from in
    // here, and it must already be able to see who connected.
    if (param && event == ESP_A2D_CONNECTION_STATE_EVT) {
      memcpy(lastPeer, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
      if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        set_last_connection(param->conn_stat.remote_bda);
        // A connection the OTHER side opened -- a car reconnecting when the
        // engine starts. While the library is scanning it drops every A2DP
        // event on the floor (bt_app_av_sm_hdlr ignores them in DISCOVERING
        // and DISCOVERED), so the link comes up and then nothing plays, which
        // is what the car reports as "connection failed". Put it in the state
        // where it listens, and stop the inquiry that is now pointless.
        if (s_a2d_state == APP_AV_STATE_DISCOVERING || s_a2d_state == APP_AV_STATE_DISCOVERED ||
            s_a2d_state == APP_AV_STATE_UNCONNECTED) {
          s_a2d_state = APP_AV_STATE_CONNECTING;
          if (discovery_active) esp_bt_gap_cancel_discovery();
        }
      }
    }
    BluetoothA2DPSource::app_a2d_callback(event, param);
  }

  void app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) override;

  uint8_t lastPeer[ESP_BD_ADDR_LEN] = { 0 };
};

static CydSource a2dp;

static BtDevice     devs[BT_MAX_DEVICES];
static int          devCount = 0;
static volatile uint32_t listSerial = 0;
static portMUX_TYPE devMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool haveTarget = false;
static uint8_t       targetAddr[6];
static char          targetName[33];

static volatile esp_a2d_connection_state_t state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;

// Nothing connected AND nothing being torn down. Paging during a disconnect is
// refused by the peer, which costs a whole retry cycle.
static bool btIdle() { return state == ESP_A2D_CONNECTION_STATE_DISCONNECTED; }

// Survives ESP.restart() (not a power cycle): the request to boot into pairing.
static const uint32_t PAIRING_MAGIC = 0xB7A1B7A1;
RTC_NOINIT_ATTR static uint32_t rtcPairing;
static bool pairingBoot = false;
static volatile bool saveWanted = false;
static volatile bool adoptTarget = false;

// A connection the owner asked for, and how it went.
static const uint32_t CONNECT_TIMEOUT_MS = 20000;

// Who is on the other end. The name cannot come from the tap alone: the car
// connected after the wait for it had already timed out, and a head unit may
// open the link itself, so the device shown was whatever connected last time.
// Ask the stack instead, and believe the answer.
static uint8_t           connectedAddr[6] = { 0 };
static volatile bool     nameWanted = false;      // ask who this is
// A name is only ever learned WITH the address it belongs to. The library asks
// the name of every device an inquiry turns up, so an unattached name is some
// passing device -- a car in the next parking space -- not the one playing.
static uint8_t           learnedAddr[6] = { 0 };
static char              learnedName[33] = { 0 };
static volatile bool     learned = false;
static uint32_t          connectDeadline = 0;      // 0 = nothing pending
// Switching from one remembered device to another: the link has to be dropped
// first, so the request is held until the radio is free (bt_tick).
static int               pendingKnown = -1;
static volatile BtFailure failure = BT_FAIL_NONE;
static volatile uint32_t  passkey = 0;

// Bluetooth task: pairing events the library only logs.
void CydSource::app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
  switch (event) {
    case ESP_BT_GAP_KEY_NOTIF_EVT:
      passkey = param->key_notif.passkey;
      Serial.printf("bt: passkey %06u -- confirm it on the device\n", param->key_notif.passkey);
      break;
    case ESP_BT_GAP_CFM_REQ_EVT:
      passkey = param->cfm_req.num_val;
      Serial.printf("bt: confirm %06u (answered yes)\n", param->cfm_req.num_val);
      break;
    case ESP_BT_GAP_PIN_REQ_EVT:
      Serial.println("bt: the device asked for a PIN (answering with the default)");
      break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
      if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
        memcpy(learnedAddr, param->read_rmt_name.bda, 6);
        strlcpy(learnedName, (const char*)param->read_rmt_name.rmt_name, sizeof(learnedName));
        learned = true;
      }
      break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
      if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
        Serial.printf("bt: paired with \"%s\"\n", (const char*)param->auth_cmpl.device_name);
        memcpy(learnedAddr, param->auth_cmpl.bda, 6);
        strlcpy(learnedName, (const char*)param->auth_cmpl.device_name, sizeof(learnedName));
        learned = true;
      } else {
        Serial.printf("bt: pairing REFUSED, status %d\n", param->auth_cmpl.stat);
        failure = BT_FAIL_AUTH;
        connectDeadline = 0;
      }
      passkey = 0;
      break;
    default:
      break;
  }
  BluetoothA2DPSource::app_gap_callback(event, param);
}

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

  // The name we are looking for, at an address we are not. The stored pair has
  // drifted -- older firmware could save one device's name beside another's
  // address, and then page a device that is not there for ever. Believe the
  // name, take the address, and let onState() write the pair back together.
  const bool renamed = !pairingBoot && !saved && !tapped &&
                       g_settings.btName.length() && g_settings.btName == name;
  if (renamed) {
    portENTER_CRITICAL(&devMux);
    memcpy(targetAddr, address, 6);
    strlcpy(targetName, name, sizeof(targetName));
    portEXIT_CRITICAL(&devMux);
    Serial.printf("bt: \"%s\" is at a different address now -- adopting it\n", name);
  }

  if (tapped || saved)
    Serial.printf("bt: found \"%s\" -- connecting\n", name);
  return tapped || saved || renamed;
}

static void onState(esp_a2d_connection_state_t s, void*) {
  static const char* NAMES[] = { "disconnected", "connecting", "connected", "disconnecting" };
  Serial.printf("bt: %s  heap %u\n", s <= 3 ? NAMES[s] : "?", ESP.getFreeHeap());
  state = s;
  if (s == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    connectDeadline = 0;
    failure = BT_FAIL_NONE;
    passkey = 0;
    const uint8_t* peer = a2dp.peer();
    memcpy(connectedAddr, peer, 6);
    bool changed = memcmp(g_settings.btAddr, peer, 6) != 0;
    // The device the owner tapped -- by address, because the wait for it may
    // have timed out before it answered, which is what the car does.
    if (targetName[0] && memcmp(targetAddr, peer, 6) == 0) {
      adoptTarget = true;                    // the String is assigned in bt_tick,
      changed = true;                        // on the UI's own task
    } else {
      nameWanted = true;                     // somebody else: ask who
    }
    haveTarget = false;
    if (changed) {
      memcpy(g_settings.btAddr, peer, 6);
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
  // Secure Simple Pairing: what a car expects. The library answers the
  // "compare this number" request for us; app_gap_callback above keeps the
  // number so the screen can show what the owner is agreeing to.
  a2dp.set_ssp_enabled(true);

  // Normal boot: page the saved speaker first. Pairing boot: go straight to
  // discovery, then turn auto-reconnect back on so the speaker the user picks
  // is written to the library's NVS for next time.
  // One attempt, not three: a device that does not answer the first page is
  // off or out of range, and three immediate tries only delay the fallback to
  // scanning. Anything later is handled by the 15 s paging in bt_tick().
  a2dp.set_auto_reconnect(!pairingBoot, 1);
  a2dp.start();
  // It stays OFF for the rest of a pairing boot. The library decides what to do
  // about ten seconds after start(): with auto-reconnect on and an address in
  // its own store it pages that device instead of discovering, so the pairing
  // screen would list nothing and hand straight back to the player -- which is
  // exactly what happens once a remembered speaker is switched on nearby. It
  // goes back on when something has been chosen; the address is written by
  // set_last_connection() in app_a2d_callback either way.

  Serial.printf("bt: started (%s), saved speaker \"%s\"  heap %u\n",
                pairingBoot ? "PAIRING" : "reconnect", g_settings.btName.c_str(),
                ESP.getFreeHeap());
}

// Add a device to the list of those connected before, or refresh its name.
// Matched by address: a device may be renamed, and two may share a name.
//
// The order is the order they were first connected, and it does not change
// when one of them connects again. Sorting by most-recent moved the row under
// the owner's finger and made the highlight look stuck on the first line.
// When the list is full the oldest goes.
static void remember(const uint8_t addr[6], const char* name) {
  Settings& g = g_settings;
  for (int i = 0; i < g.btKnownCount; i++) {
    if (memcmp(g.btKnown[i].addr, addr, 6) == 0) {
      strlcpy(g.btKnown[i].name, name, sizeof(g.btKnown[i].name));
      return;
    }
  }
  if (g.btKnownCount == BT_KNOWN_MAX) {
    for (int i = 0; i + 1 < BT_KNOWN_MAX; i++) g.btKnown[i] = g.btKnown[i + 1];
    g.btKnownCount--;
  }
  BtKnown& slot = g.btKnown[g.btKnownCount++];
  memcpy(slot.addr, addr, 6);
  strlcpy(slot.name, name, sizeof(slot.name));
}

void bt_tick() {
  const uint8_t key = pendingKey;
  if (key) {
    pendingKey = 0;
    handleSpeakerKey(key);
  }

  // The link asked to be dropped is gone: now page what was asked for.
  if (pendingKnown >= 0 && btIdle()) {
    const int i = pendingKnown;
    pendingKnown = -1;
    bt_connectKnown(i);
  }

  // Reach for the saved device rather than waiting to stumble across it.
  // Discovery only finds what is advertising, and a car advertises only while
  // its pairing screen is open -- so "looking for MBUX" would wait for ever
  // once it had been disconnected. A known device is paged by address, which
  // is what a phone does.
  static uint32_t nextReconnect = 0;
  static const uint8_t NO_ADDR[6] = { 0 };
  if (!pairingBoot && !haveTarget && btIdle() &&
      memcmp(g_settings.btAddr, NO_ADDR, 6) != 0 &&
      (int32_t)(millis() - nextReconnect) >= 0) {
    nextReconnect = millis() + 15000;
    Serial.printf("bt: paging saved device \"%s\"\n", g_settings.btName.c_str());
    a2dp.connectNow(g_settings.btAddr, g_settings.btName.c_str());
  }

  // Be findable, not just looking. Ten seconds after it starts, the library
  // turns the scan mode off entirely -- neither discoverable nor connectable --
  // to keep a peer-initiated connection from confusing its state machine. On
  // the pairing screen that is backwards: a car head unit expects to find
  // "CYD-MP3" in its OWN list, and offers no other way in. So re-assert it
  // while that screen is up, and only there: a pairing boot, nothing connected.
  static uint32_t nextVisible = 0;
  if (!bt_connected() && (int32_t)(millis() - nextVisible) >= 0) {
    nextVisible = millis() + 5000;
    // Connectable always, so the car can open the link itself when the engine
    // starts; findable as well only while the pairing screen is up.
    if (pairingBoot) a2dp.set_discoverability(ESP_BT_GENERAL_DISCOVERABLE);
    else             a2dp.set_connectable(true);
  }

  // Give up on a connection the owner asked for, rather than saying
  // "connecting" until the board is restarted.
  if (connectDeadline && (int32_t)(millis() - connectDeadline) >= 0) {
    // Still negotiating? Then it is not stuck, it is slow -- a car takes its
    // time. Only give up once the stack has stopped trying.
    if (bt_connecting()) {
      connectDeadline = millis() + CONNECT_TIMEOUT_MS;
      return;
    }
    connectDeadline = 0;
    haveTarget = false;
    passkey = 0;
    if (failure == BT_FAIL_NONE) failure = BT_FAIL_TIMEOUT;
    Serial.printf("bt: giving up on \"%s\"\n", targetName);
    a2dp.rediscover();
  }

  // Find out who connected, when it was not the device that was tapped. The
  // list from the last scan usually knows; otherwise ask over the air.
  if (nameWanted) {
    nameWanted = false;
    bool found = false;
    BtDevice d;
    for (int i = 0; i < bt_deviceCount() && !found; i++) {
      if (bt_device(i, d) && memcmp(d.addr, connectedAddr, 6) == 0) {
        memcpy(learnedAddr, d.addr, 6);
        strlcpy(learnedName, d.name, sizeof(learnedName));
        learned = found = true;
      }
    }
    if (!found) esp_bt_gap_read_remote_name(connectedAddr);
  }

  if (learned) {
    learned = false;
    // Only the device actually on the other end may rename what is shown.
    if (bt_connected() && memcmp(learnedAddr, connectedAddr, 6) == 0 && learnedName[0]) {
      if (g_settings.btName != learnedName) {
        g_settings.btName = learnedName;
        Serial.printf("bt: connected device is \"%s\"\n", learnedName);
      }
      remember(connectedAddr, learnedName);
      saveWanted = true;
    }
  }

  // Chosen and connected during a pairing boot: let the library reconnect to
  // it by itself from the next boot on.
  static bool reconnectRestored = false;
  if (pairingBoot && !reconnectRestored && bt_connected()) {
    reconnectRestored = true;
    a2dp.set_auto_reconnect(true, 1);
  }

  // Once something has answered, the start-up reconnect has done its job. From
  // here on a dropped link is handled by the paging below -- one attempt every
  // 15 s at the device that is actually wanted -- rather than by the library
  // throwing three immediate attempts at whatever was connected before.
  static bool chaseStopped = false;
  if (!chaseStopped && bt_connected()) {
    chaseStopped = true;
    a2dp.chaseLastDevice(false);
  }

  if (saveWanted) {
    saveWanted = false;
    if (adoptTarget) {
      g_settings.btName = targetName;
      remember(connectedAddr, targetName);
      adoptTarget = false;
    }
    settings_save();
    Serial.printf("bt: saved speaker \"%s\"\n", g_settings.btName.c_str());
  }
}

bool   bt_connected()   { return state == ESP_A2D_CONNECTION_STATE_CONNECTED; }
bool   bt_connecting()  { return state == ESP_A2D_CONNECTION_STATE_CONNECTING; }
String bt_speakerName() { return g_settings.btName; }
bool   bt_pairingBoot() { return pairingBoot; }
bool   bt_choosing()    { return haveTarget; }
BtFailure bt_failure()  { return failure; }
uint32_t  bt_passkey()  { return passkey; }
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

bool bt_isConnectedTo(const uint8_t addr[6]) {
  return bt_connected() && memcmp(connectedAddr, addr, 6) == 0;
}

int bt_knownCount() { return g_settings.btKnownCount; }

bool bt_known(int i, BtDevice& out) {
  if (i < 0 || i >= g_settings.btKnownCount) return false;
  memcpy(out.addr, g_settings.btKnown[i].addr, 6);
  strlcpy(out.name, g_settings.btKnown[i].name, sizeof(out.name));
  out.rssi = 0;                          // not seen, remembered
  return true;
}

void bt_connectKnown(int i) {
  BtDevice d;
  if (!bt_known(i, d)) return;
  if (bt_connected()) {
    if (memcmp(connectedAddr, d.addr, 6) == 0) return;     // already on it
    // Claim the target before dropping the link: it stops the library chasing
    // what we are leaving, and stops our own paging (which still points at the
    // old address until the new one connects) from doing the same.
    portENTER_CRITICAL(&devMux);
    memcpy(targetAddr, d.addr, 6);
    strlcpy(targetName, d.name, sizeof(targetName));
    haveTarget = true;
    portEXIT_CRITICAL(&devMux);
    pendingKnown = i;
    failure = BT_FAIL_NONE;
    connectDeadline = millis() + CONNECT_TIMEOUT_MS;
    Serial.printf("bt: switching to \"%s\"\n", d.name);
    a2dp.chaseLastDevice(false);
    a2dp.disconnect();
    return;
  }
  portENTER_CRITICAL(&devMux);
  memcpy(targetAddr, d.addr, 6);
  strlcpy(targetName, d.name, sizeof(targetName));
  haveTarget = true;
  portEXIT_CRITICAL(&devMux);
  failure = BT_FAIL_NONE;
  passkey = 0;
  connectDeadline = millis() + CONNECT_TIMEOUT_MS;
  Serial.printf("bt: paging \"%s\" on request\n", d.name);
  a2dp.connectNow(d.addr, d.name);
}

void bt_forget(int i) {
  if (i < 0 || i >= g_settings.btKnownCount) return;
  uint8_t addr[6];
  memcpy(addr, g_settings.btKnown[i].addr, 6);
  Serial.printf("bt: forgetting \"%s\"\n", g_settings.btKnown[i].name);

  for (int j = i; j + 1 < g_settings.btKnownCount; j++)
    g_settings.btKnown[j] = g_settings.btKnown[j + 1];
  g_settings.btKnownCount--;

  // Forgetting has to mean forgetting. Dropped from the list but still the
  // "last device", it would be paged again every 15 s and reconnected to at
  // the next boot -- so the link goes, our record goes, and the library's own
  // stored address goes with them.
  pendingKnown = -1;

  // The link goes first, by address: the library's disconnect() reads the very
  // record the next few lines erase, and would otherwise hang up on nobody
  // while the music kept playing through the device just deleted.
  if (bt_isConnectedTo(addr)) {
    Serial.println("bt: it was the one connected -- dropping the link");
    a2dp.disconnectFrom(addr);
  }

  if (memcmp(g_settings.btAddr, addr, 6) == 0) {
    static const uint8_t NONE[6] = { 0 };
    memcpy(g_settings.btAddr, NONE, 6);
    g_settings.btName = "";
    haveTarget = false;
    targetName[0] = 0;
    connectDeadline = 0;
    a2dp.clean_last_connection();
  }

  // And the pairing itself: with the bond still in the stack the two would
  // know each other, and either side could open the link again.
  const esp_err_t err = esp_bt_gap_remove_bond_device((uint8_t*)addr);
  if (err != ESP_OK) Serial.printf("bt: remove bond -> %s\n", esp_err_to_name(err));

  settings_save();
}

void bt_choose(int i) {
  BtDevice d;
  if (!bt_device(i, d)) return;
  portENTER_CRITICAL(&devMux);
  memcpy(targetAddr, d.addr, 6);
  memcpy(targetName, d.name, sizeof(targetName));
  haveTarget = true;
  portEXIT_CRITICAL(&devMux);
  failure = BT_FAIL_NONE;
  passkey = 0;
  connectDeadline = millis() + CONNECT_TIMEOUT_MS;
  Serial.printf("bt: chose \"%s\" -- connecting now\n", d.name);
  // onSsid() still connects if this fails and the device turns up again.
  a2dp.connectNow(d.addr, d.name);
}

#endif  // !CYD_UPLOADER
