# Building and distributing firmware

How to flash your own board, how to produce a single `.bin` other people can
flash, and how they flash it.

Nothing user-specific is compiled in. Settings, touch calibration, the saved
speaker and WiFi network live in NVS on the device; songs, backgrounds and the
title font live on the SD card. That is what makes one binary shareable.

---

## 1. The two images

The flash holds **two firmware images** (see `app/firmware.h` and
[docs/decisions.md](docs/decisions.md)):

| Partition | Offset | Image | Built with |
|---|---|---|---|
| app0 (ota_0) | `0x10000` | player — Bluetooth, MP3, UI | default |
| app1 (ota_1) | `0x1F0000` | uploader — WiFi file manager | `-DCYD_UPLOADER` |

The partition scheme is **`min_spiffs`** (two 1.9 MB app slots). NVS stays at
`0x9000`, size `0x5000`, as in every other scheme — settings survive a change
of scheme.

Both come from the one sketch in `app/`.

## 2. Flashing your own board (keeps settings)

```bash
./tools/flash.sh app
```

This compiles the player and uploads it with arduino-cli (which also writes the
bootloader, partition table and OTA selector), then compiles the uploader and
writes it to `0x1F0000` with esptool, then opens the serial monitor.

It uploads at **115200 baud**. This board's CH340 USB chip fails the switch to
921600 (`A fatal error occurred ... Invalid head of packet`); nothing is written
when that happens, so a failed fast upload is harmless — just slow to retry.

Plain `arduino-cli upload` writes only the player. The board works, but
**Setup → Upload music** will report "Uploader not installed".

Writing the partition table resets the OTA selector to app0, so a flash always
boots the player.

## 3. Making a release

```bash
./tools/make_release.sh 1.1.1
```

Set `FIRMWARE_VERSION` in `app/firmware.h` first; the script refuses a version
that does not match it. Produces `release/cyd-mp3-v1.1.1-4mb.bin` and adds its
line to `release/SHA256SUMS`, keeping earlier releases' lines: a full 4 MB
image with bootloader, partition table, OTA selector and **both** apps at their
offsets, so the person flashing it needs one command and one address.

**The merged image at `0x0` is a factory reset.** Its gaps are `0xFF`, and those
cover NVS: settings, calibration, the saved speaker and WiFi network are erased.
That is right for a clean install and wrong for an upgrade — use section 2 for
your own board.

The script refuses to finish unless **each slot holds the right program**: the
player at `0x10000`, the uploader at `0x1F0000`, each checked for a log line only
it contains and for the absence of the other's. An image with the uploader in
the player's slot boots only into upload mode, and "Done" cannot leave it.

The title font is **not** part of a release. It is rasterised from a system font
whose licence does not permit redistribution; each owner generates their own
with `tools/gen_cjk_font.py` (README, step 2).

## 4. Instructions for the person flashing

They need [esptool](https://github.com/espressif/esptool) — no Arduino toolchain:

```bash
pip install esptool
```

Find the port (the board is a CH340: `/dev/cu.usbserial-*` on macOS,
`/dev/ttyUSB0` on Linux, `COM3`-ish on Windows). Use **one** USB cable — the 2.8"
board has micro-USB and USB-C wired to the same chip, and both at once breaks
uploads.

Erase, then write:

```bash
esptool --port /dev/cu.usbserial-1420 erase-flash
```

```bash
esptool --port /dev/cu.usbserial-1420 --baud 115200 write-flash 0x0 cyd-mp3-v1.1.1-4mb.bin
```

At 115200 a 4 MB image takes a few minutes. Older esptool (v4) spells the
commands `erase_flash` / `write_flash`.

Check the download first if a checksum was published:

```bash
shasum -a 256 -c SHA256SUMS
```

Then follow the README from **Prepare the SD card**.

## 5. Notes

- **Wipe settings without reflashing:** erase just NVS. The next boot runs touch
  calibration and asks for a speaker again.

  ```bash
  esptool --port /dev/cu.usbserial-1420 erase-region 0x9000 0x5000
  ```

- **Stuck in the uploader?** It cannot happen through normal use: the uploader
  points the next boot back at the player as soon as it starts, so any reset or
  power cut returns to the player.
- **Boot loop after an update?** The serial log (115200) says why. The usual
  cause while developing was heap: watch the `stat:` lines' `min` value, which
  should stay above ~15 KB with the speaker connected.
