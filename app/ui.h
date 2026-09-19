// ===========================================================================
// ui.h -- drawing helpers shared by every screen, and the screen switcher
// ===========================================================================
// Icons are drawn from primitives (triangles, rects, circles) rather than
// bitmaps: they scale to any button size, take the theme's colours for free,
// and cost no flash for a second set when stage 4 adds larger layouts.
#pragma once

#include <Arduino.h>
#include "geometry.h"
#include "tokens.h"
#include "theme.h"
#include "style.h"
#include "touch.h"
#include "i18n.h"

enum Icon : uint8_t {
  ICON_NONE, ICON_PREV, ICON_PLAY, ICON_PAUSE, ICON_NEXT,
  ICON_VOL_DOWN, ICON_VOL_UP, ICON_GEAR, ICON_BLUETOOTH, ICON_BACK, ICON_NOTE,
  ICON_SHUFFLE, ICON_REPEAT, ICON_REPEAT_ONE, ICON_UP, ICON_DOWN, ICON_LIST,
  ICON_CHEVRON
};

// Which face a piece of chrome is set in. The style resolves each to a
// TFT_eSPI font number (style.h). Song titles are deliberately absent: they
// come from 18 px strips so Chinese and English keep the same height.
enum TypeRole : uint8_t { TYPE_BODY, TYPE_NUMERIC, TYPE_CAPTION };

uint8_t ui_font(TypeRole role);

// The numeric face, unless it cannot carry this particular string. Two ways it
// can fail: font 7 is a seven-segment display -- digits, colon, period, minus
// and space and nothing else -- so "2:09 / 3:27" would draw as gaps; and a
// face taller than `maxHeight` would spill out of its box over whatever is
// under it. Either way the body face takes over. maxHeight 0 = no ceiling.
uint8_t ui_numericFont(const String& s, int maxHeight = 0);

// `size` is the icon's nominal height in px.
void ui_icon(Icon icon, int cx, int cy, int size, uint16_t fg, uint16_t bg);

// A button in the shape the current style asks for (style.h), icon centred.
// `active` is a control that is on (playing, selected); `pressed` is one with a
// finger on it right now.
void ui_button(const Rect& r, Icon icon, bool active = false, bool pressed = false);

// The progress track, in the style's form: solid, knobbed, segmented or dotted.
// `px` is how much of `r.w` has played. `surface` is what `r` is sitting on --
// the knob overhangs the track, so it has to know what to wipe.
void ui_bar(const Rect& r, int px, uint16_t surface);

// A word in a button face instead of an icon.
// `surface` is what the button sits on: the screen, or a dialog's panel.
void ui_textButton(const Rect& r, Txt label, bool primary = false, bool pressed = false,
                   int32_t surface = -1);

// The up/down arrows down the right edge of a long list.
void ui_pager(const Rect& up, const Rect& down);

// A borderless toggle sitting on a header band: the face appears only when on.
void ui_iconToggle(const Rect& r, Icon icon, bool on, uint16_t surface, bool pressed = false);

// An icon and a short value sharing one box -- the speaker state and volume.
void ui_chip(const Rect& r, Icon icon, uint16_t iconColour, const String& text,
             uint16_t surface);

// A switch, right-aligned in `r`. The caller clears the surface first.
void ui_switch(const Rect& r, bool on);

// A value the finger can drag along `r`. Follows the style's bar idiom.
void ui_slider(const Rect& r, int value, int maxValue, uint16_t surface);

// ---------------------------------------------------------------------------
// List rows
// ---------------------------------------------------------------------------
// Paint a row's surface in the style's divider idiom (zebra, hairline, box or
// nothing) and return the colour its content must be drawn against. Shared by
// Setup's settings list and the track list, which carry different content on
// the same surface.
uint16_t ui_rowSurface(const Rect& r, int index, bool active = false, bool pressed = false);

// What a row's value is, and therefore what tapping the row will do. Today
// every Setup row looks the same whether it cycles, navigates or opens a
// dialog; the affordance on the right is what tells them apart.
enum RowKind : uint8_t { RK_NAV, RK_CYCLE, RK_TOGGLE, RK_SLIDER, RK_INFO };

struct RowValue {
  RowKind kind;
  Txt     label;      // a translated value, or T_COUNT when `text` is used
  String  text;
  int     value, max; // slider position and range; value != 0 is a toggle on
};

RowValue ui_nav(const String& text = String());
RowValue ui_cycle(Txt label);
RowValue ui_cycle(const String& text);
RowValue ui_info(const String& text);
RowValue ui_toggleValue(bool on);
RowValue ui_sliderValue(int value, int maxValue);

// Where a RK_SLIDER row's track runs. Drawing and hit-testing both take it
// from here, so where the finger lands is where the value goes -- deriving it
// twice is how a slider ends up half a label out of step with itself.
Rect ui_rowTrack(const Rect& r);

// One settings row: label at the left, value and its affordance at the right.
void ui_row(const Rect& r, int index, Txt label, const RowValue& v, bool pressed = false);

// Redraw only a slider row's right half -- the track and its value -- as a drag
// moves it. The label is left alone: in Chinese it is a strip read from the
// card, and repainting it on every step of a drag would hammer the SD bus the
// audio is reading from.
void ui_rowSlider(const Rect& r, int index, int value, int maxValue);

// Text in any script. Pure ASCII is drawn with the built-in font; anything
// else from its pre-rendered strip (titles.h), falling back to the built-in
// font (which drops non-ASCII) when no strip exists. `datum` is a TFT_eSPI
// datum (TL/TC/TR/ML/MC/MR). maxW > 0 clips. Returns the width drawn.
int ui_text(const String& text, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg,
            int maxW = 0, TypeRole role = TYPE_BODY);

// A UI string in the current language -- English when no Chinese strip exists.
int ui_label(Txt id, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg, int maxW = 0,
             TypeRole role = TYPE_BODY);

// Header bar with a back arrow at the left and a title.
static const int UI_HEADER_H = 30;
static const Rect UI_BACK_RECT = { 0, 0, 56, UI_HEADER_H };
void ui_header(Txt title, const String& suffix = String());

// "1:23", or "1:02:03" past an hour; "--:--" for unknown.
String ui_time(uint32_t ms, bool known = true);

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
enum ScreenId : uint8_t { SCR_PLAYER, SCR_SETUP, SCR_BLUETOOTH, SCR_LIBRARY, SCR_COUNT };

// What a finger is doing right now, for feedback while it is down. Gestures
// (TouchEvent) are classified on release; this is everything before that.
enum PressPhase : uint8_t { PRESS_DOWN, PRESS_MOVE, PRESS_UP };

struct Screen {
  void (*enter)();
  void (*leave)();
  void (*tick)(uint32_t now);
  void (*touch)(TouchEvent ev, int x, int y);
  // Optional. Light what is under the finger on DOWN, follow a drag on MOVE,
  // restore on UP -- which always arrives before the gesture's event, so a
  // screen the tap switches to is never drawn over. Return true to claim the
  // press as a drag: its tap or long-press is then not delivered.
  bool (*press)(PressPhase phase, int x, int y);
};

extern const Screen SCREEN_PLAYER, SCREEN_SETUP, SCREEN_BLUETOOTH, SCREEN_LIBRARY;

void     ui_go(ScreenId id);          // leave the current screen, enter this one
ScreenId ui_current();
void     ui_tick(uint32_t now);
void     ui_touch(TouchEvent ev, int x, int y);
void     ui_redraw();                 // re-enter the current screen (after calibration)
// True while the press under way has been taken as a drag. A drag can easily
// outlast the 4 s that makes a hold mean "recalibrate", and must not.
bool     ui_pressClaimed();

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------
// There is no framebuffer to restore from, so an overlay ends by redrawing the
// screen under it whole. While one is up the screen's tick is paused (audio
// and Bluetooth are ticked from the loop and carry on), so nothing is painted
// over it, and the next tap goes to the overlay.

// A short message across the bottom of the screen. Goes by itself after `ms`,
// or at the next tap, which it swallows.
void ui_toast(Txt text, uint32_t ms = 2500);

// A question with Cancel and an action button. `onOk` runs if the owner
// confirms; either way the screen underneath is redrawn.
typedef void (*ConfirmFn)();
void ui_confirm(Txt title, Txt line1, Txt line2, Txt okLabel, ConfirmFn onOk);
