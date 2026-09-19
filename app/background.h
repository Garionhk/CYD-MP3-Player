// ===========================================================================
// background.h -- animated backgrounds: convert GIFs once, play the result
// ===========================================================================
// Decided in stage 0 (docs/stage0_results.md): decoding a GIF live costs
// ~25 KB of LZW state the heap cannot spare beside Bluetooth. So:
//
//   BOOT, before Bluetooth   bg_convertAll()
//     each /bg/bgN.gif without an up-to-date /.sys/bgN.anim is decoded,
//     scaled to COVER 240x240 (or 240x320 when the GIF is tall), centre-
//     cropped, and the pixels each frame paints are written out.
//   PLAYBACK                 bg_tick()
//     read those spans, push them to the panel -- about 2 KB of RAM.
//
// Every size is accepted (owner's choice): small overshoots are cropped, big
// GIFs shrunk. Masks are screen rectangles the background never paints, which
// is how text and buttons drawn over it stay flicker-free without a
// framebuffer.
#pragma once

#include <Arduino.h>
#include "geometry.h"

// Progress callback for the boot screen: GIF n of total, frames done so far.
typedef void (*BgProgressFn)(int n, int total, int frame);

void bg_convertAll(BgProgressFn progress);
int  bg_count();                      // bg1..bgN present on the card

// Where the background plays, and what it must not paint over. The pointer is
// kept, so masks must outlive the view (layouts are static tables).
void bg_setView(const Rect& area, const Rect* masks, int maskCount);

bool bg_start(int n);                 // open /.sys/bgN.anim; false if missing
void bg_stop();
bool bg_active();
int  bg_current();                    // 0 when stopped

// Draw the next frame when it is due. Backs off while audio is short of data.
void bg_tick(uint32_t now);

// Diagnostics: frames drawn and average ms per frame since the last call.
void bg_takeStats(uint32_t& frames, uint32_t& avgMs);
void bg_takeMax(uint32_t& ms, uint32_t& bytes);   // slowest / largest frame
