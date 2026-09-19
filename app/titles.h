// ===========================================================================
// titles.h -- song titles in any script, drawn from pre-rendered strips
// ===========================================================================
// TFT_eSPI's built-in fonts are ASCII-only. A CJK font covering real song
// titles is ~1.8 MB (on the SD card, /.sys/cjk16.vlw, made by
// tools/gen_cjk_font.py) and holds ~85 KB of metrics in RAM while loaded --
// affordable at boot, impossible once Bluetooth has the heap.
//
// So titles get the same treatment as backgrounds:
//   BOOT, before Bluetooth   titles_prepareAll()
//     each title without a cached strip is rendered, anti-aliased, into an
//     18 px tall 2-bit bitmap at /.sys/t/<crc32 of the title>.ttl
//   PLAYBACK                 titles_load() + titles_draw()
//     the current title's strip (<= 3.3 KB) is read into RAM and drawn in
//     the theme's colours, scrolling if it is wider than its box
//
// Keyed by the title TEXT, so identical titles share a strip and a title that
// changes (stage 2 reads ID3 tags) simply gets a new one.
#pragma once

#include <Arduino.h>
#include "geometry.h"

static const int TITLE_STRIP_H = 18;

typedef void (*TitleProgressFn)(int done, int total);

// Returns false when there is no font on the card (titles then fall back to
// the built-in ASCII font).
bool titles_prepareAll(TitleProgressFn progress);

// Load the strip for `text` into RAM. False if none cached.
bool titles_load(const String& text);
bool titles_loaded();
int  titles_width();

// Draw the loaded strip into `box`, starting `scrollX` px in. Past the end of
// the strip comes `gap` px of background, then the strip again -- a marquee.
void titles_draw(const Rect& box, int scrollX, int gap, uint16_t fg, uint16_t bg);
