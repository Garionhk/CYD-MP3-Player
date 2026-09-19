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
static int  maxRows()  { return min(BT_MAX_DEVICES, (tft.height() - LIST_Y) / ROW_H); }

static uint32_t shownList = 0xFFFFFFFF;
static int      shownState = -1;

static int stateNow() {
  return bt_connected() ? 2 : (bt_connecting() || bt_choosing()) ? 1 : 0;
}

static void drawStatus() {
  const Palette& p = theme();
  const int y = STATUS_Y + 13;
  tft.fillRect(0, STATUS_Y, tft.width(), 26, p.bg);
  if (bt_connected()) {
    const int w = ui_label(T_CONNECTED, 10, y, ML_DATUM, p.good, p.bg);
    ui_text(bt_speakerName(), 10 + w + 6, y, ML_DATUM, p.good, p.bg, tft.width() - w - 26);
  } else if (bt_choosing() || bt_connecting()) {
    ui_label(T_CONNECTING, 10, y, ML_DATUM, p.warn, p.bg);
  } else if (bt_pairingBoot()) {
    ui_label(T_PAIRING_HINT, 10, y, ML_DATUM, p.text, p.bg, tft.width() - 20);
  } else if (bt_speakerName().length()) {
    const int w = ui_label(T_LOOKING_FOR, 10, y, ML_DATUM, p.dim, p.bg);
    ui_text(bt_speakerName(), 10 + w + 6, y, ML_DATUM, p.dim, p.bg, tft.width() - w - 26);
  } else {
    ui_label(T_PAIRING_MODE, 10, y, ML_DATUM, p.text, p.bg, tft.width() - 20);
  }
}

static void drawList() {
  const Palette& p = theme();
  const int n = min(bt_deviceCount(), maxRows());
  tft.fillRect(0, LIST_Y, tft.width(), tft.height() - LIST_Y, p.bg);
  if (n == 0 && !bt_connected()) ui_label(T_SEARCHING, 10, LIST_Y + 14, ML_DATUM, p.dim, p.bg);
  for (int i = 0; i < n; i++) {
    BtDevice d;
    if (!bt_device(i, d)) continue;
    const int y = LIST_Y + i * ROW_H;
    tft.fillRoundRect(6, y + 2, tft.width() - 12, ROW_H - 4, 6, p.btn);
    ui_text(d.name, 14, y + ROW_H / 2, ML_DATUM, p.text, p.btn, tft.width() - 90);
    tft.setTextColor(p.dim, p.btn);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(String(d.rssi) + " dBm", tft.width() - 14, y + ROW_H / 2, 2);
  }
}

static void enter() {
  const Palette& p = theme();
  tft.fillScreen(p.bg);
  ui_header(T_BLUETOOTH);
  const Rect r = pairRect();
  tft.fillRoundRect(r.x + 2, r.y + 3, r.w - 4, r.h - 6, 6, p.btnActive);
  ui_label(T_PAIR_NEW, r.cx(), r.cy(), MC_DATUM, p.btnText, p.btnActive, r.w - 8);
  shownState = -1;
  shownList = 0xFFFFFFFF;
}

static void tick(uint32_t) {
  if (stateNow() != shownState) {
    shownState = stateNow();
    drawStatus();
    if (shownState == 2 && bt_pairingBoot()) { ui_go(SCR_PLAYER); return; }
  }
  if (bt_listSerial() != shownList) {
    shownList = bt_listSerial();
    drawList();
  }
}

static void touch(TouchEvent ev, int x, int y) {
  if (ev != TOUCH_TAP) return;
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
  if (y >= LIST_Y && !bt_connected()) {
    const int i = (y - LIST_Y) / ROW_H;
    if (i < bt_deviceCount() && i < maxRows()) {
      bt_choose(i);
      drawStatus();
    }
  }
}

const Screen SCREEN_BLUETOOTH = { enter, nullptr, tick, touch };

#endif  // !CYD_UPLOADER
