# Project decisions

Choices that shaped the architecture, and why. Newest first. The measurements
behind them are in [stage0_results.md](stage0_results.md).

The constant behind almost all of them: the ESP32-2432S028R has **no PSRAM**.
With the Bluetooth stack (~80 KB) and the MP3 decoder (~33 KB) running, the
player has ~30 KB of heap to spare, and a Bluetooth connection briefly needs a
good part of that.

## 2026-09-17 — Player and uploader are separate firmware images

**Context.** Upload mode was first a restart flag inside one binary: boot with
the flag and start WiFi instead of Bluetooth. It worked as designed and broke
the player. Linking the WiFi stack costs ~21 KB of **static** RAM (`.bss` in
libnet80211, wpa_supplicant, lwIP — `g_cnxMgr` alone is 3.9 KB) whether WiFi is
ever started or not. Free heap at boot fell from 209 KB to 186 KB, the minimum
at Bluetooth connect fell to ~12 KB, and the board reset in a loop.

Making the web server objects lazy recovered nothing measurable; the cost is in
the driver's data, not in anything the sketch allocates.

**Decision.** Two images from one sketch. `min_spiffs` gives two 1.9 MB app
slots: the player in app0, the uploader (built with `-DCYD_UPLOADER`) in app1.
"Upload music" sets the boot partition to app1 and restarts; "Done" sets it
back. Player-only sources are wrapped in `#ifndef CYD_UPLOADER` and vice versa,
so neither image links the other's stack. Both share NVS and the SD card.

**Consequences.** Flashing is two writes (`tools/flash.sh` does both) and a
release is a merged image carrying both. The uploader points the next boot at
the player the moment it starts, so a crash or power cut in upload mode can
never strand the device there. A board flashed with only the player says so in
Setup instead of restarting into an empty partition.

## 2026-09-17 — Backgrounds store only what changes, and read at a budget

**Context.** One GIF converted to 114 KB per frame: at 11 fps that is 1.25 MB/s
of SD reads, more than the card delivers (~870 KB/s, stage 0). The decode task
shares that card, and the music dropped out.

Drawing each frame in slices that pause when the audio buffer is low was tried
first and was **worse** -- 212 underruns in 7 minutes against 4. Slicing does not
reduce the reading; it only breaks it into smaller pieces.

**Decision.** Two changes instead.

- The converter keeps a canvas of what the screen will show and writes only the
  pixels a frame actually changes (format version 2). Measured: 2788 KB -> 1356
  KB on a 39-frame GIF. The canvas is ~120 KB of small per-row allocations, made
  before Bluetooth starts and freed straight after; it stops allocating with
  48 KB of heap left and simply stores whole frames if it cannot fit.
- Playback paces frames so the background reads at most ~350 KB/s. A heavy
  animation plays slower rather than taking the card away from the music.

**Result on the owner's board:** 104-frame GIF at 7.6 fps, smooth, and no
underruns during steady playback (13 in 9 minutes, in two bursts at song
changes).

## 2026-09-16 — Song titles and Chinese menus are pre-rendered strips

**Context.** TFT_eSPI's built-in fonts are ASCII. A `.vlw` font covering Big5
and GB2312 level 1 is 1.8 MB (on the card, fine) but holds ~12 bytes of metrics
per glyph in RAM while loaded — ~85 KB, impossible beside Bluetooth.

**Decision.** At boot, before Bluetooth, every song label and every Chinese UI
string without a cached strip is rendered once into an 18 px, 2-bit
anti-aliased bitmap at `/.sys/t/<crc32>.ttl`; the font is then unloaded. At run
time a strip (≤3.3 KB) is loaded and drawn in the theme's colours. Strips are
keyed by the text, so a changed ID3 title or a new UI string simply gets a new
one; a marker holding the library and UI-string checksums skips the whole check
when nothing changed.

The font is generated per owner by `tools/gen_cjk_font.py` from a system font,
because those fonts cannot be redistributed. Without it everything still works
in English/ASCII.

## 2026-09-16 — Library index on the card, not in RAM

A `String` per path is ~70 KB for a thousand songs. `/.sys/library.idx` holds
`path\ttitle\tartist` per line; RAM keeps a 4-byte offset per track. At boot
`/music` is walked and a CRC of paths and sizes is compared with the index
header; ID3 tags are only re-read when that changes.

ID3v1 is not read: Chinese v1 tags are usually GBK or Big5 with nothing saying
which, and a wrong guess is worse than the file name.

## 2026-09-16 — Play order belongs to the UI task

The decode task opens what it is asked to and reports "track ended". Shuffle,
repeat and "what is next" live in `audio_tick()` on the loop task, so the
order table is never shared between tasks and needs no locking.

## 2026-09-16 — Audio ring buffer stays at 2048 frames

Halving it to 1024 (8 KB saved) underran ~80×/s with a background playing. Not
because Bluetooth takes much at once — it pulls 128 frames per call — but
because the background's reads share the SD card with the decoder, and 23 ms is
too little cushion. 2048 (46 ms) runs at 0 underruns.

## 2026-09-16 — Bluetooth reconnects by address; pairing is a restart

After an ESP32 reset the JBL did not appear in discovery until put back in
pairing mode, so "scan for the saved name" would strand the owner every boot.
The A2DP library pages the saved address instead. During its discovery
fallback the saved address is also accepted, which is how a speaker switched on
later gets picked up.

The library does not restart discovery after a deliberate disconnect, so "Pair
new" restarts into a pairing boot (auto-reconnect off) — the exact path proven
in stage 0 — rather than fighting its state machine.

Speaker buttons (AVRCP pass-through) arrive on the Bluetooth task and are only
recorded there; `bt_tick()` acts on them from the loop task.

## 2026-09-16 — Backgrounds are converted once, not decoded live

AnimatedGIF holds ~25 KB of LZW state. Beside Bluetooth and the decoder that left
~10 KB. Instead, before Bluetooth starts, each `/bg/bgN.gif` is decoded,
scaled to cover 240×240 (or 240×320 for tall GIFs), centre-cropped, and the
pixels each frame paints are written to `/.sys/bgN.anim`. Playback reads spans
and pushes them — ~2 KB. Source size and timestamp in the header decide when to
reconvert. AnimatedGIF is vendored with `MAX_WIDTH 2048` so phone-sized GIFs can
be shrunk; its `gif.inl` is renamed because the Arduino builder does not copy
`.inl` files from a sketch's `src/`.

Text and buttons over a background are **masked** out of the spans, so they are
drawn once and never flicker, with no framebuffer.

The owner chose "accept every size" over "240×240 / 240×320 only" after the first
real GIFs turned out to be 250×250 and 498×373.

## 2026-09-16 — SD card on VSPI, touch bit-banged

The ESP32 has two free SPI hosts. The display has HSPI; the weather clock gave
VSPI to touch because it never used the SD slot. Streaming audio needs the card
on hardware SPI, so the XPT2046 (polled, happy at a few hundred kHz) is clocked
by hand. Touch calibration is stored in the panel's native portrait coordinates
so one calibration serves every orientation.

## 2026-09-16 — Reuse the weather clock's board handling

`config/board.h`, the runtime ST7789/ILI9341 switch, the boot-hold recovery
gesture, the NVS settings pattern and the touch calibration wizard all come from
the "CYD 2.8 Weather Clock Bus" project, where they were already proven on these
boards. The TFT_eSPI `User_Setup.h` templates and their compile-time
cross-checks came along too.
