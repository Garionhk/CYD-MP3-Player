// Player image only (firmware.h).
#ifndef CYD_UPLOADER

// ===========================================================================
// screen_bt.cpp -- Bluetooth speaker
// ===========================================================================
// Shows the current speaker. While nothing is connected the library is
// discovering, so every device it finds is listed and a tap connects to it.
// "Pair new" restarts into a pairing boot (bt.h explains why a restart).
// Sized from the screen, so it works in either orientation.

#include "ui.h"
#include "display.h"
#include "theme.h"
#include "bt.h"

static const int STATUS_Y = UI_HEADER_H + 4;
static const int LIST_Y   = UI_HEADER_H + 34;
static const int ROW_H    = 28;

static Rect pairRect() { return { tft.width() - 110, 0, 110, UI_HEADER_H }; }
// The cross at the right of a saved row: what deletes it.
static const int DROP_W = 30;
static Rect dropRect(int row) {
  return { tft.width() - 6 - DROP_W, LIST_Y + row * ROW_H + 2, DROP_W, ROW_H - 4 };
}
static int  maxRows()  { return min(BT_MAX_DEVICES, (tft.height() - LIST_Y) / ROW_H); }

static uint32_t shownList = 0xFFFFFFFF;
static int      shownState = -1;
// The name is written a moment after the link comes up, by which time the
// status line has already been drawn -- so it is watched, not assumed.
static String   shownName;
static bool     pairLit = false;           // Pair new under a finger

// A pairing boot hands back to the player the moment the new speaker connects
// -- but only if this screen was opened waiting for that. pairingBoot stays
// true for the whole session, so testing the state alone bounced every later
// visit straight back out, and the owner could never get here to pair another.
static bool waitingToPair = false;

static int stateNow() {
  const int base = bt_connected() ? 2 : (bt_connecting() || bt_choosing()) ? 1 : 0;
  // Fold in what else the status line shows, so it redraws when they change.
  return base + 10 * (int)bt_failure() + 100 * (bt_passkey() ? 1 : 0);
}

static void drawStatus() {
  const Palette& p = theme();
  const int y = STATUS_Y + 13;
  tft.fillRect(0, STATUS_Y, tft.width(), 26, p.bg);
  if (bt_connected()) {
    const int w = ui_label(T_CONNECTED, 10, y, ML_DATUM, p.good, p.bg);
    ui_text(bt_speakerName(), 10 + w + 6, y, ML_DATUM, p.good, p.bg, tft.width() - w - 26);
  } else if (bt_choosing() || bt_connecting()) {
    const int w = ui_label(T_CONNECTING, 10, y, ML_DATUM, p.warn, p.bg);
    // A car shows a number and waits for it to be agreed to. The library says
    // yes for us, so the owner has to be able to see what was agreed.
    if (bt_passkey()) {
      const int c = ui_label(T_CONFIRM_CODE, 10 + w + 8, y, ML_DATUM, p.dim, p.bg);
      char code[8];
      snprintf(code, sizeof(code), "%06u", (unsigned)bt_passkey());
      ui_text(code, 10 + w + c + 14, y, ML_DATUM, p.text, p.bg);
    }
  } else if (bt_failure() != BT_FAIL_NONE) {
    ui_label(bt_failure() == BT_FAIL_AUTH ? T_PAIR_REFUSED : T_CONNECT_FAILED,
             10, y, ML_DATUM, p.bad, p.bg, tft.width() - 20);
  } else if (bt_pairingBoot()) {
    ui_label(T_PAIRING_HINT, 10, y, ML_DATUM, p.text, p.bg, tft.width() - 20);
  } else if (bt_speakerName().length()) {
    const int w = ui_label(T_LOOKING_FOR, 10, y, ML_DATUM, p.dim, p.bg);
    ui_text(bt_speakerName(), 10 + w + 6, y, ML_DATUM, p.dim, p.bg, tft.width() - w - 26);
  } else {
    ui_label(T_PAIRING_MODE, 10, y, ML_DATUM, p.text, p.bg, tft.width() - 20);
  }
}

// The list is the devices connected before, then whatever discovery has found
// that is not already among them. A remembered device can be paged whenever
// the owner asks; a found one has to be tapped while it is still advertising.
static bool knownAddr(const uint8_t addr[6]) {
  BtDevice k;
  for (int i = 0; i < bt_knownCount(); i++)
    if (bt_known(i, k) && memcmp(k.addr, addr, 6) == 0) return true;
  return false;
}

// Row `row` of the list: a remembered device, or a newly found one.
// Returns false past the end. `found` is its index in the discovered list.
static bool rowDevice(int row, BtDevice& out, bool& remembered, int& found) {
  remembered = row < bt_knownCount();
  found = -1;
  if (remembered) return bt_known(row, out);
  int skip = row - bt_knownCount();
  for (int i = 0; i < bt_deviceCount(); i++) {
    BtDevice d;
    if (!bt_device(i, d) || knownAddr(d.addr)) continue;
    if (skip-- == 0) { out = d; found = i; return true; }
  }
  return false;
}

static int rowCount() {
  int n = bt_knownCount();
  for (int i = 0; i < bt_deviceCount(); i++) {
    BtDevice d;
    if (bt_device(i, d) && !knownAddr(d.addr)) n++;
  }
  return min(n, maxRows());
}

static void drawList() {
  const Palette& p = theme();
  const int n = rowCount();
  tft.fillRect(0, LIST_Y, tft.width(), tft.height() - LIST_Y, p.bg);
  if (n == 0 && !bt_connected()) ui_label(T_SEARCHING, 10, LIST_Y + 14, ML_DATUM, p.dim, p.bg);
  for (int i = 0; i < n; i++) {
    BtDevice d;
    bool remembered;
    int found;
    if (!rowDevice(i, d, remembered, found)) continue;
    const int y = LIST_Y + i * ROW_H;
    const bool playing = bt_isConnectedTo(d.addr);   // by address, not by name
    const uint16_t face = playing ? p.btnActive : p.btn;
    const uint16_t ink  = playing ? p.onControlActive : p.text;
    tft.fillRoundRect(6, y + 2, tft.width() - 12, ROW_H - 4, 6, face);
    const int right = tft.width() - (remembered ? 14 + DROP_W : 14);
    ui_text(d.name, 14, y + ROW_H / 2, ML_DATUM, ink, face, right - 100);
    if (remembered) {
      ui_label(playing ? T_CONNECTED : T_SAVED, right, y + ROW_H / 2, MR_DATUM,
               playing ? p.onControlActive : p.dim, face);
      // A cross, so deleting is something the owner can see rather than a
      // press they have to be told about.
      const Rect x = dropRect(i);
      const int cx = x.cx(), cy = x.cy(), a = 5;
      for (int k = 0; k < 2; k++) {
        tft.drawLine(cx - a, cy - a + k, cx + a, cy + a + k, p.dim);
        tft.drawLine(cx - a, cy + a - k, cx + a, cy - a - k, p.dim);
      }
    } else {
      ui_text(String(d.rssi) + " dBm", right, y + ROW_H / 2, MR_DATUM, p.dim, face, 0,
              TYPE_CAPTION);
    }
  }
}

static void drawPairButton() {
  ui_textButton(pairRect(), T_PAIR_NEW, true, pairLit, theme().band);
}

static void enter() {
  tft.fillScreen(theme().bg);
  ui_header(T_BLUETOOTH);
  pairLit = false;
  drawPairButton();
  shownState = -1;
  shownList = 0xFFFFFFFF;
  shownName = "";
  waitingToPair = bt_pairingBoot() && !bt_connected();
}

static void tick(uint32_t) {
  if (stateNow() != shownState || shownName != bt_speakerName()) {
    shownState = stateNow();
    shownName = bt_speakerName();
    drawStatus();
    if (shownState == 2 && waitingToPair) {
      waitingToPair = false;
      ui_go(SCR_PLAYER);
      return;
    }
    drawList();                            // connected or not changes what it says
  }
  if (bt_listSerial() != shownList) {
    shownList = bt_listSerial();
    drawList();
  }
}

// The row whose cross was tapped, waiting on the dialog's answer.
static int dropRow = -1;
static void dropConfirmed() {
  if (dropRow >= 0) bt_forget(dropRow);
  dropRow = -1;
}

static void touch(TouchEvent ev, int x, int y) {
  if (ev != TOUCH_TAP && ev != TOUCH_LONG_PRESS) return;
  if (UI_BACK_RECT.contains(x, y)) {
    // Leaving a pairing boot without choosing: go back to normal reconnecting.
    if (bt_pairingBoot() && !bt_connected()) bt_cancelPairing();
    ui_go(SCR_PLAYER);
    return;
  }
  if (pairRect().contains(x, y)) {
    bt_requestPairing();                 // restarts
    return;
  }
  if (y >= LIST_Y) {
    const int row = (y - LIST_Y) / ROW_H;
    BtDevice d;
    bool remembered;
    int found;
    if (row >= rowCount() || !rowDevice(row, d, remembered, found)) return;
    if (ev != TOUCH_TAP) return;
    if (remembered && dropRect(row).contains(x, y)) {
      dropRow = row;
      ui_confirm(T_FORGET_Q, T_FORGET_HINT, T_COUNT, T_FORGET, dropConfirmed);
      return;
    }
    if (remembered) bt_connectKnown(row);   // page it, switching if need be
    else            bt_choose(found);
    drawStatus();
    drawList();
  }
}

static bool press(PressPhase phase, int x, int y) {
  const bool on = phase == PRESS_DOWN && pairRect().contains(x, y);
  if (on != pairLit && phase != PRESS_MOVE) {
    pairLit = on;
    drawPairButton();
  }
  return false;
}

const Screen SCREEN_BLUETOOTH = { enter, nullptr, tick, touch, press };

#endif  // !CYD_UPLOADER
