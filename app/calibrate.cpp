#include "calibrate.h"
#include "touch.h"
#include "display.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"
#include <math.h>

static const int TARGET_INSET   = 25;
static const int SAMPLES_WANTED = 24;
static const int ACCEPT_ERR_PX  = 25;
static const int MAX_ATTEMPTS   = 3;
static const uint32_t PRESS_TIMEOUT_MS = 60000;

// The wizard runs in rotation 0: native 240 x 320.
static const int W = CYD_PANEL_W, H = CYD_PANEL_H;

static void drawTarget(int x, int y, uint16_t colour) {
  tft.drawCircle(x, y, 12, colour);
  tft.drawCircle(x, y, 6, colour);
  tft.drawFastHLine(x - 16, y, 33, colour);
  tft.drawFastVLine(x, y - 16, 33, colour);
}

// Text goes through ui_label, so the wizard speaks the chosen language once
// the card's strips exist -- and English on a first boot, before they do.
static void message(Txt line1, Txt line2, uint16_t colour) {
  tft.fillScreen(theme().bg);
  ui_label(line1, W / 2, H / 2 - 14, MC_DATUM, colour, theme().bg, W - 10);
  if (line2 != T_COUNT) ui_label(line2, W / 2, H / 2 + 12, MC_DATUM, theme().dim, theme().bg, W - 10);
}

static bool readStablePress(long& rx, long& ry) {
  const uint32_t deadline = millis() + PRESS_TIMEOUT_MS;
  while ((int32_t)(millis() - deadline) < 0) {
    int x, y, z;
    if (touch_readRaw(x, y, z)) {
      long sx = 0, sy = 0;
      int n = 0;
      while (n < SAMPLES_WANTED && touch_readRaw(x, y, z)) {
        sx += x; sy += y; n++;
        delay(4);
      }
      if (n >= SAMPLES_WANTED) { rx = sx / n; ry = sy / n; return true; }
    }
    delay(5);
  }
  return false;
}

static void waitForRelease() {
  uint32_t quietSince = millis();
  while (millis() - quietSince < 150) {
    int x, y, z;
    if (touch_readRaw(x, y, z)) quietSince = millis();
    delay(5);
  }
}

static bool collectCorners(TouchCal& out) {
  const int xL = TARGET_INSET, xR = W - 1 - TARGET_INSET;
  const int yT = TARGET_INSET, yB = H - 1 - TARGET_INSET;
  const int tx[4] = { xL, xR, xR, xL };                 // TL, TR, BR, BL
  const int ty[4] = { yT, yT, yB, yB };
  static const Txt NAMES[4] = { T_CAL_TL, T_CAL_TR, T_CAL_BR, T_CAL_BL };
  long rx[4], ry[4];

  for (int i = 0; i < 4; i++) {
    tft.fillScreen(theme().bg);
    drawTarget(tx[i], ty[i], theme().accent);
    ui_label(NAMES[i], W / 2, H / 2 - 12, MC_DATUM, theme().text, theme().bg);
    ui_label(T_CAL_HOLD, W / 2, H / 2 + 12, MC_DATUM, theme().dim, theme().bg, W - 10);
    Serial.printf("calibrate: target %d/4 (%s) at %d,%d\n", i + 1, tr_en(NAMES[i]), tx[i], ty[i]);
    if (!readStablePress(rx[i], ry[i])) {
      Serial.println("calibrate: timed out waiting for a press");
      return false;
    }
    drawTarget(tx[i], ty[i], theme().good);
    Serial.printf("calibrate:   raw %ld,%ld\n", rx[i], ry[i]);
    waitForRelease();
    delay(120);
  }

  // Which raw axis drives native x? The one that swings more across the width.
  const long rawXacrossWidth = labs(((rx[1] + rx[2]) / 2) - ((rx[0] + rx[3]) / 2));
  const long rawXdownHeight  = labs(((rx[2] + rx[3]) / 2) - ((rx[0] + rx[1]) / 2));
  out.swapXY = rawXdownHeight > rawXacrossWidth;

  const long aL = out.swapXY ? (ry[0] + ry[3]) / 2 : (rx[0] + rx[3]) / 2;
  const long aR = out.swapXY ? (ry[1] + ry[2]) / 2 : (rx[1] + rx[2]) / 2;
  const long bT = out.swapXY ? (rx[0] + rx[1]) / 2 : (ry[0] + ry[1]) / 2;
  const long bB = out.swapXY ? (rx[2] + rx[3]) / 2 : (ry[2] + ry[3]) / 2;

  // Targets are inset: extrapolate to the true edges.
  const float slopeX = float(aR - aL) / float(xR - xL);
  const float slopeY = float(bB - bT) / float(yB - yT);
  out.xAt0 = lround(aL - slopeX * xL);
  out.xAtW = lround(aL + slopeX * ((W - 1) - xL));
  out.yAt0 = lround(bT - slopeY * yT);
  out.yAtH = lround(bT + slopeY * ((H - 1) - yT));

  const bool sane = labs(out.xAtW - out.xAt0) > 500 && labs(out.yAtH - out.yAt0) > 500;
  if (!sane) Serial.println("calibrate: corner spread too small -- bad presses");
  return sane;
}

static bool confirmTap(int& errPx) {
  const int cx = W / 2, cy = H / 2;
  tft.fillScreen(theme().bg);
  drawTarget(cx, cy, theme().accent);
  ui_label(T_CAL_CONFIRM, W / 2, H - 30, MC_DATUM, theme().text, theme().bg, W - 10);

  long rx, ry;
  if (!readStablePress(rx, ry)) return false;
  int px, py;
  touch_nativePoint(rx, ry, px, py);
  errPx = (int)lround(sqrt(double((px - cx) * (px - cx) + (py - cy) * (py - cy))));
  tft.fillCircle(px, py, 4, errPx <= ACCEPT_ERR_PX ? theme().good : theme().bad);
  Serial.printf("calibrate: confirm tap at %d,%d (%d px off)\n", px, py, errPx);
  waitForRelease();
  return errPx <= ACCEPT_ERR_PX;
}

bool calibrate_run() {
  Serial.println("calibrate: starting wizard");
  const TouchCal previous = touch_calibration();
  display_setRotation(0);

  message(T_CAL_TITLE, T_CAL_PRESS_EACH, theme().text);
  delay(1200);
  const int noise = touch_measureNoiseFloor();

  bool accepted = false;
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    TouchCal c = previous;
    c.zThreshold = max(300, noise + 150);
    c.valid = true;

    if (!collectCorners(c)) {
      touch_setCalibration(previous, false);
      message(T_CAL_SKIPPED, T_CAL_KEEPING, theme().warn);
      delay(1500);
      break;
    }
    touch_setCalibration(c, false);          // apply for the confirm tap, not saved yet

    int errPx = 0;
    if (confirmTap(errPx)) {
      touch_setCalibration(c, true);
      Serial.printf("calibrate: accepted x[%ld..%ld] y[%ld..%ld] swap=%d z>=%d (noise %d)\n",
                    c.xAt0, c.xAtW, c.yAt0, c.yAtH, c.swapXY, c.zThreshold, noise);
      message(T_CAL_DONE, T_COUNT, theme().good);
      delay(900);
      accepted = true;
      break;
    }
    if (attempt < MAX_ATTEMPTS) {
      message(T_CAL_NOT_QUITE, T_CAL_TRY_AGAIN, theme().warn);
      tft.setTextColor(theme().dim, theme().bg);
      tft.setTextDatum(MC_DATUM);
      tft.drawString(String(errPx) + " px", W / 2, H / 2 + 36, 2);
      delay(1500);
    } else {
      // Approximate touch beats none; a 4 s hold re-runs this any time.
      touch_setCalibration(c, true);
      message(T_CAL_ROUGH, T_CAL_REDO, theme().warn);
      delay(1800);
      accepted = true;
    }
  }

  display_setRotation(g_settings.rotation);
  return accepted;
}
