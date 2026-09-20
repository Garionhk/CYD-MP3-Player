// ===========================================================================
// bt.h -- Bluetooth A2DP source: the speaker link
// ===========================================================================
// Reconnecting. The A2DP library keeps the last speaker's address in NVS and,
// with auto-reconnect on, pages it directly at start-up (3 tries), then falls
// back to discovery. Paging matters on this hardware: stage 0 found the JBL
// does not show up in a discovery scan after the ESP32 resets unless it is put
// back in pairing mode. During the discovery fallback the saved address is
// still accepted, so a speaker switched on later is picked up if it becomes
// discoverable.
//
// Pairing a different speaker. After a deliberate disconnect the library does
// not restart discovery by itself, so "pair new speaker" restarts the board
// into a pairing boot: auto-reconnect off at start, discovery runs, every
// device found is listed, and the one the user taps is connected and saved.
// A restart is ~3 s and uses exactly the path proven in stage0/s02-s05.
#pragma once

#include <Arduino.h>

static const int BT_MAX_DEVICES = 8;

struct BtDevice {
  char    name[33];
  uint8_t addr[6];
  int     rssi;
};

void   bt_begin();
void   bt_tick();                     // call from loop()

bool   bt_connected();
bool   bt_connecting();
String bt_speakerName();

// How a connection the owner asked for ended, if it did not succeed.
enum BtFailure : uint8_t {
  BT_FAIL_NONE = 0,
  BT_FAIL_TIMEOUT,                    // nothing came back in time
  BT_FAIL_AUTH,                       // the device refused the pairing
};

// Pairing
bool   bt_pairingBoot();              // this boot was started to pair a speaker
void   bt_requestPairing();           // restart into a pairing boot (does not return)
void   bt_cancelPairing();            // restart into a normal boot (does not return)
int    bt_deviceCount();
bool   bt_device(int i, BtDevice& out);

// ---------------------------------------------------------------------------
// Devices connected before, most recent first (Settings::btKnown)
// ---------------------------------------------------------------------------
// A car and a speaker take turns, and neither can be found by discovery once
// it is out of pairing mode -- a car only advertises while its pairing screen
// is open. Keeping the addresses means either can be paged on request.
// Is this the device currently on the other end? By address: names are not
// unique, and the stored name lags a moment behind a switch.
bool   bt_isConnectedTo(const uint8_t addr[6]);

int    bt_knownCount();
bool   bt_known(int i, BtDevice& out);
void   bt_connectKnown(int i);        // page it now
void   bt_forget(int i);              // drop it from the list
uint32_t bt_listSerial();             // changes whenever the list does
void   bt_choose(int i);
bool   bt_choosing();                 // a device was tapped, waiting for it

// Why the last attempt the owner made failed; cleared by the next attempt.
BtFailure bt_failure();
// A Secure Simple Pairing code to compare with the one on the other device,
// or 0. A car shows one and waits; the owner cannot confirm what they cannot
// see, so the screen has to display it.
uint32_t  bt_passkey();
