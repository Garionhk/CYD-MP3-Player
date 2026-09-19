// ===========================================================================
// display.h -- the panel: init (either controller), rotation, backlight
// ===========================================================================
// Ported from the weather clock's panel.cpp. Boards sold as ESP32-2432S028R
// carry an ST7789 or an ILI9341 behind identical glass; one build drives both
// by sending the other controller's init sequence by hand when
// Settings::panel disagrees with the compiled-in driver. The full argument is
// in the clock project's panel.h and docs/decisions.md.
#pragma once

#include <TFT_eSPI.h>
#include "board.h"

extern TFT_eSPI tft;

// tft.init(), the controller patch if needed, rotation and inversion from
// settings. Leaves the backlight OFF -- call display_setBacklight() after the
// first frame is drawn.
void display_begin();

// Rotation 0-3. Writes the right MADCTL byte for whichever panel is fitted.
void display_setRotation(uint8_t rot);

// 0-100 %. PWM on the backlight pin.
void display_setBacklight(uint8_t percent);

const char* display_panelName(uint16_t panelId);

// Blind recovery for a board showing nothing: a finger held from power-up for
// 8 s switches controller and restarts; the blue LED blinks from 4 s to say it
// is about to happen. Returns in a few ms when nobody is touching.
void display_bootGesture();
