// ===========================================================================
// tokens.h -- the measurements every screen shares
// ===========================================================================
// Spacing, radii, icon sizes and row heights used to be literals scattered
// through four screens: the row height was 30 in Setup, 35 in the Library and
// 29 in the List skin for no reason anyone could name. They live here now, so
// a measurement is decided once and cited everywhere.
//
// This file holds sizes only. Colours are the theme's (theme.h) and shapes are
// the style's (style.h); between the three of them a screen should never need
// a literal.
#pragma once

// ---------------------------------------------------------------------------
// Spacing. Whatever a screen puts between two things is one of these, picked
// by role rather than by eye.
// ---------------------------------------------------------------------------
static const int SP_HAIR = 1;    // a rule or a bevel edge
static const int SP_XS   = 2;    // inside an icon
static const int SP_S    = 4;    // a button face inset from its touch rect
static const int SP_M    = 8;    // between related things
static const int SP_L    = 12;   // a text block's margin from its edge
static const int SP_XL   = 16;   // between groups
static const int SP_XXL  = 24;   // a screen's breathing room

// ---------------------------------------------------------------------------
// Corner radii. A style picks one (style.h); screens never choose.
// ---------------------------------------------------------------------------
static const int RADIUS_SHARP = 0;
static const int RADIUS_SOFT  = 4;
static const int RADIUS_ROUND = 8;
static const int RADIUS_PILL  = 11;

// ---------------------------------------------------------------------------
// Icon nominal heights, in px -- the `size` argument to ui_icon().
// ---------------------------------------------------------------------------
static const int ICON_SM = 14;   // in a header or a status chip
static const int ICON_MD = 18;   // a row's toggle, a library header button
static const int ICON_LG = 24;   // a player transport button
static const int ICON_XL = 64;   // the placeholder note when nothing plays

// A fingertip is about this wide on 2.8" glass. Nothing tappable should be
// smaller; the List skin's 35 px mini-player buttons are the one deliberate
// exception, and they are hit by thumb against a screen edge.
static const int TOUCH_MIN = 40;

// ---------------------------------------------------------------------------
// Lists.
// ---------------------------------------------------------------------------
static const int UI_ROW_H       = 32;   // Setup and the Library
static const int UI_ROW_H_TIGHT = 29;   // the List skin's mini-player
static const int UI_PAGER_W     = 44;   // the up/down arrows down the right edge

// Note: the screens still carry their own row constants until they move to
// ui_row(); these are what they will adopt then, not what they use today.
