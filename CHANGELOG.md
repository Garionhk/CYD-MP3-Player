# Changelog

What shipped. Why it was built that way is in [docs/decisions.md](docs/decisions.md).

## 1.0.0 — 2026-09-17

First release. Everything below was verified on an ESP32-2432S028R (ST7789)
with a JBL Flip 4.

### Playback
- MP3 from the SD card to a Bluetooth speaker (A2DP source). 44.1/48/32/22 kHz,
  mono or stereo, CBR or VBR; durations from Xing/Info/VBRI headers.
- Play/pause, next, previous (restart within 3 s), volume, seek ±10 s, tap the
  progress bar to jump; time shows elapsed, remaining or both.
- Library index on the card with ID3v2.2/2.3/2.4 titles and artists; shuffle,
  repeat all/one/off; resume track and position after a restart or power cut.
- Speaker buttons (AVRCP): play/pause, next, previous.
- Reconnects to the saved speaker by address, including after the speaker is
  switched off and on. "Pair new" for a different speaker.

### Screen
- Animated backgrounds from any GIF: converted once at boot, cropped or shrunk
  to fit, drawn around the controls without flicker. Frames store only what
  changes and are paced to a read budget, so a heavy animation never starves
  the audio.
- Song titles and menus in any script from a font on the card, rendered once to
  anti-aliased strips.
- 8 themes, 6 control styles and 3 layouts (Classic, Big buttons, List), each
  in 4 orientations; English / 繁體中文, brightness, colour inversion.
- List layout and Library page with ▲/▼ arrows (hold to jump to start or end).
- ST7789 and ILI9341 panels from one build: Setup → Panel type, or an 8 s hold at
  power-on for a screen that shows nothing.
- On-device touch calibration, stored once for all orientations.

### Upload mode
- Separate WiFi firmware image: browser file manager for music (upload, rename,
  delete, folders), backgrounds (thumbnails, upload, delete with renumbering) and
  the title font; home WiFi or the player's own hotspot with captive portal.

### Tools
- `tools/flash.sh app` builds and flashes both images.
- `tools/make_release.sh <version>` produces one merged 4 MB image.
- `tools/gen_cjk_font.py` builds the title font from a system CJK font.
