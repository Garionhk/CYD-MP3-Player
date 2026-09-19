# CYD Bluetooth MP3 Player

Firmware that turns a **"cheap yellow display"** ESP32 board into a touch-screen
MP3 player: songs from the SD card, played to a **Bluetooth speaker or
headphones**, with an animated background, song titles in any script, and a
WiFi page for adding music from a phone or laptop.

- **Plays MP3s from the SD card** over Bluetooth (A2DP). 44.1/48/32/22 kHz,
  mono or stereo, CBR or VBR. Resumes the song and position after a power cut.
- **Touch controls**: play/pause, next, previous, volume, seek (hold ⏮/⏭), tap
  the progress bar to jump, tap the time to show elapsed / remaining / both.
- **Library** with shuffle and repeat (all / one / off). Titles and artists come
  from ID3 tags, falling back to the file name.
- **Speaker buttons work**: play/pause and next/previous on the speaker itself
  control the player. Switch the speaker off and on and it reconnects on its own.
- **Animated backgrounds** from any GIF, any size — cropped or shrunk to fit.
- **Looks**: 8 colour themes, 6 control styles and 3 layouts, mixed freely, in
  portrait or landscape either way up.
- **English or 繁體中文** for every menu. Song titles show Chinese, Japanese and
  Latin text, anti-aliased.
- **Upload mode**: add, rename and delete songs and backgrounds from a browser,
  over your home WiFi or the player's own hotspot — no computer or card reader
  needed after the first setup.

**Board: ESP32-2432S028R** (2.8", 240×320, resistive touch, micro-USB + USB-C).
Both panel versions sold under that name — **ST7789** and **ILI9341** — work with
the same firmware; see [Panel type](#panel-type).

[flash.md](flash.md) covers building, flashing and publishing releases.
[docs/decisions.md](docs/decisions.md) explains why it is built the way it is, and
[docs/stage0_results.md](docs/stage0_results.md) has the hardware measurements
those decisions rest on.

---

## Quick start

### 1. Prepare the SD card

A FAT32 card (tested with 2 GB). Create:

```
/music     your MP3s (folders allowed, two levels deep)
/bg        background GIFs named bg1.gif, bg2.gif, bg3.gif ...
```

The player creates `/.sys` itself for its own files. You can skip the card
reader later and add everything over WiFi — but the player needs at least one
song to be useful, so put a few on now.

### 2. Make the title font (once)

Song titles and Chinese menus are drawn from a font file on the card. It is
generated from a font already on your computer (the licence of those fonts does
not allow shipping one), so each owner makes their own:

```bash
python3 -m pip install Pillow
```

```bash
python3 tools/gen_cjk_font.py
```

This writes `sdcard/.sys/cjk16.vlw` (about 1.8 MB, 7,000+ Traditional and
Simplified characters, kana, Latin). Copy it to the card as `/.sys/cjk16.vlw`,
or upload it later from the **Title font** tab in upload mode. Without it the
player still works; titles show Latin letters only and menus stay in English.

### 3. Flash the firmware

Either flash a published release (one file, see [flash.md](flash.md)), or build
from source — see [Building](#building) below.

### 4. First boot

1. **Touch calibration.** Press and hold each corner target, then tap the centre.
   Once only; hold anywhere for 4 seconds to redo it later.
2. **"Preparing song titles"** and **"Preparing background"** screens. The first
   boot after adding songs or GIFs does some one-off work; later boots skip it.
3. **Bluetooth.** Put your speaker in pairing mode and tap its name. From then on
   it reconnects by itself.

---

## Using it

### Player

| Touch | Does |
|---|---|
| ⏯ ⏮ ⏭ | play/pause, previous (restart if more than 3 s in), next |
| hold ⏮ / ⏭ | seek back / forward 10 s |
| 🔉 🔊 | volume −/+ 5 % |
| progress bar | jump to that point |
| time | cycle elapsed · remaining · both |
| song title | open the Library |
| ⚙ | Setup |
| hold anywhere 4 s | re-run touch calibration |

### Library

Tap a song to play it. ▲/▼ page through; hold them to jump to the start or end.
The buttons at top right are **shuffle** and **repeat** (all → one → off).

The **List** layout puts the same list on the player screen, with its own ▲/▼
beside it and the mini-player underneath.

### Setup

| Row | Values |
|---|---|
| Bluetooth speaker | current speaker; **Pair new** to switch |
| Upload music (WiFi) | restarts into upload mode |
| Theme | Dark, Light, Neon, Retro Amber, Ocean, Phosphor, Paper, Pop |
| Style | Classic, Frosted deck, Swiss grid, Cassette pop, Terminal, Paper mono — the shape of buttons, rows and the progress bar |
| Layout | Classic, Big buttons, List — where things sit |
| Background | off, bg1 … bgN |
| Orientation | landscape, landscape flipped, portrait, portrait flipped |
| Brightness | 100 → 20 % |
| Language | English / 中文 |
| Invert colours | for panels whose colours come out inverted |
| Panel type | ST7789 / ILI9341 — tap twice to switch and restart |
| Calibrate touch | re-run the wizard |
| About | version, song count, free memory |

### Upload mode

**Setup → Upload music (WiFi)** restarts into a separate WiFi program (see
[Why two programs](#why-two-programs)).

- **No home WiFi saved:** the player starts its own open hotspot,
  `CYD-MP3-XXXX`. Join it from a phone or laptop; the page opens by itself, or
  browse to **http://192.168.4.1**.
- **Home WiFi saved:** it joins your network and shows its address, e.g.
  `http://192.168.0.42`.

The page has four tabs:

- **Music** — upload MP3s (several at once, or drag and drop), rename, delete,
  make folders.
- **Backgrounds** — thumbnails of your GIFs; upload or delete. Uploads are named
  bg1.gif, bg2.gif … automatically, and deleting one renumbers the rest.
- **Title font** — install or replace `cjk16.vlw`.
- **WiFi** — scan and save your home network (2.4 GHz only).

Tap **Done** on the page or the screen to go back to the player, which picks up
the changes as it starts. A power cut or reset during upload mode also returns
to the player.

The page has no password: anyone on the same network, or near enough to join
the hotspot, can manage the card's files **while upload mode is on**. It is off
the rest of the time.

---

## Panel type

Boards sold as ESP32-2432S028R carry either an ST7789 or an ILI9341 display
controller behind identical glass. The firmware is built for the ST7789 and
drives the ILI9341 by sending its start-up sequence at run time.

If your screen is blank, mirrored or garbled on first boot, **hold a finger on
the screen while powering on**. On a new board the blue LED lights for 3 seconds
— press while it is lit — then blinks from 4 s; keep holding to 8 s and the
board switches controller and restarts. The same hold switches back.

If the screen is readable, **Setup → Panel type** does the same with two taps.
Colours inverted? **Setup → Invert colours**.

---

## Building

Requires [arduino-cli](https://arduino.github.io/arduino-cli/) with the ESP32
core (tested with 3.3.10) and these libraries:

```bash
arduino-cli core install esp32:esp32
```

```bash
arduino-cli lib install TFT_eSPI
```

The Bluetooth and MP3 libraries are not in the Arduino library index; clone them
into your libraries folder:

```bash
cd ~/Documents/Arduino/libraries && git clone --depth 1 --branch v1.8.11 https://github.com/pschatzmann/ESP32-A2DP.git
```

```bash
cd ~/Documents/Arduino/libraries && git clone --depth 1 --branch v0.9.4 https://github.com/pschatzmann/arduino-libhelix.git
```

AnimatedGIF is vendored in `app/src/AnimatedGIF` (with a raised width limit), so
it does not need installing.

**Display config — the step that catches everyone.** TFT_eSPI is configured by a
header inside the library. Copy this project's template over it, and again after
every TFT_eSPI update:

```bash
cp config/User_Setup_2432S028R_ST7789.h.template ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
```

The build stops with an explanation if it is missing or wrong.

**Build and flash** (compiles both programs, flashes both, opens the serial
monitor):

```bash
./tools/flash.sh app
```

Or just check everything compiles:

```bash
./tools/build_all.sh
```

---

## Why two programs

The board has no PSRAM, so RAM is the constraint behind most of the design. The
Bluetooth stack takes ~80 KB and the MP3 decoder ~33 KB. Linking the WiFi stack
into the same program costs another ~21 KB of fixed memory *even if WiFi is never
turned on* — and that was enough to make the Bluetooth connection fail. So the
flash holds two programs: the **player** (Bluetooth, no WiFi) and the **uploader**
(WiFi, no Bluetooth), sharing settings and the SD card. Upload mode switches
between them with a restart.

The same constraint is why backgrounds and song titles are prepared at boot,
before Bluetooth starts, rather than decoded while playing. The details and
measurements are in [docs/decisions.md](docs/decisions.md).

---

## Troubleshooting

| Symptom | Try |
|---|---|
| Upload fails at "Changing baud rate to 921600" | This board's USB chip cannot do it; `tools/flash.sh` already uses 115200 |
| "No SD card" | FAT32, fully inserted; exFAT cards will not mount |
| Titles show only Latin letters, menus stay English | `/.sys/cjk16.vlw` missing — see step 2 |
| Speaker not found | Put it in pairing mode; **Setup → Bluetooth speaker → Pair new** |
| Speaker does not reconnect after power-on | Wait ~30 s; if not, pairing mode once |
| "uploader not flashed" in Setup | Only the player was flashed — use `tools/flash.sh app` or a release image |
| Audio drops out | Watch the serial log's `underruns` count; try background off to compare |

Serial log: 115200 baud. Every 10 s it prints free memory, audio buffer fill and
underruns, and background frame rate.
