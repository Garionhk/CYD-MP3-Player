# Project decisions

Choices that shaped the architecture, and why. Newest first. The measurements
behind them are in [stage0_results.md](stage0_results.md).

The constant behind almost all of them: the ESP32-2432S028R has **no PSRAM**.
With the Bluetooth stack (~80 KB) and the MP3 decoder (~33 KB) running, the
player has ~30 KB of heap to spare, and a Bluetooth connection briefly needs a
good part of that.

## 2026-09-20 — The player owns the Bluetooth connection, not the library

**Context.** Everything worked with a speaker and nothing worked with a car.
A speaker answers every inquiry, so the library's model -- discover, then
connect to what discovery accepted -- was never tested against a device that
announces itself once and goes quiet. Against a car head unit it failed at
every step: a tap was recorded and waited for a second sighting that never
came; the peer address was taken from the last discovery rather than from the
connection, so the car's audio played while the screen named the speaker; the
board stopped being connectable ten seconds after boot, so the car could not
open the link either; and any disconnection made the library page the device
just left, which beat the one the owner had asked for.

**Decision.** The connection is ours. A tap pages the device immediately,
through the library's own path so its state machine stays in step. The peer
address comes from the A2DP connection event, and is written back to the
library so its start-up reconnect agrees with ours. A known device is paged by
address every 15 s rather than scanned for. The board stays connectable, and a
connection the peer opens is put into a state the library will act on. Chasing
the previously connected device is start-up behaviour only, and one attempt.

**Consequences.** A subclass of `BluetoothA2DPSource` reaches four protected
members (`peer_bd_addr`, `s_peer_bdname`, `s_a2d_state`, `discovery_active`)
and overrides `app_a2d_callback` and `app_gap_callback`. That is a dependency
on the library's internals, and a version bump may need it revisited -- the
alternative was a fork. Addresses and names are stored together, five of them,
because they had drifted apart once and stranded the player on a device that
was not there.

## 2026-09-18 — A release is checked slot by slot

**Context.** A merged image went out of the build with the uploader in app0 and
app1 empty. Flashed, the board booted only into upload mode: the uploader
points the next boot at app0 as it starts, which was itself, so neither "Done"
nor a reset could leave. `make_release.sh` passed it, because its check only
asked whether the uploader was somewhere in the file. Being a full 4 MB image,
it also erased the settings and the saved speaker.

**Decision.** The script checks each slot: app0 must contain a log line only
the player has and not the uploader's, app1 the reverse. It deletes the image
if either fails, and refuses a version that differs from `FIRMWARE_VERSION`.

## 2026-09-18 — The look is data: tokens, styles, components

**Context.** Layouts were already data (a new skin cost no drawing code), but
the shape of a button was hard-coded in `ui_button()`, so a new look meant new
C++, and screens that wanted a different control re-drew their own. Sizes were
literals: the same list row was 30, 35 and 29 px on three screens.

**Decision.** Three layers. `tokens.h` holds sizes. `theme` holds colour, widened
from 13 roles to 20 (surfaces, outlines, pressed, on-accent). `style` holds
shape — radius, inset, border, shadow, bevel, divider, bar form, fonts — as a
table row, chosen in Setup independently of Theme. Components in `ui.h` read
all three. Classic reproduces the old look pixel for pixel, as the fallback.

**Constraints that shaped it.** No framebuffer, so "frosted" is a raised surface
and a highlight line, not a blur, and overlays end by redrawing the screen.
Song titles stay 18 px in every style: they are pre-rendered strips shared by
Chinese and Latin text. A style's numeric font falls back when too tall for its
box or missing a glyph — font 7 has no "/", and at 48 px fits no time box.

**Touch.** Gestures are still classified on release; that code is unchanged.
Screens get an optional press hook for feedback and drags, fed by a new live
finger position. A drag is exempt from the 4 s recalibrate hold, which a slow
brightness adjustment would otherwise cross.

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
