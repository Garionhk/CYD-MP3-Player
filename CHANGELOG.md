# Changelog

What shipped. Why it was built that way is in [docs/decisions.md](docs/decisions.md).

## 1.1.1 — 2026-09-20

Bluetooth. Connecting to a car exposed a set of faults that a speaker never
had: a speaker answers every scan, a car announces itself once and goes quiet.

### Fixed
- **Tapping a device connects to it.** It used to be recorded and then waited
  for, connecting only if the scan happened to report that device a second
  time. A car never does, so the screen said "connecting" for ever and nothing
  was ever sent to it.
- **The connected device is identified correctly.** The peer address was read
  from the library's last discovery rather than from the connection itself, so
  connecting to a car saved the speaker's address and name — the car played
  while the screen named the speaker, and the next start-up reconnected to the
  wrong device.
- **Reconnecting works after a disconnection.** A known device is now paged by
  address every 15 s, instead of scanning for one that is no longer
  advertising.
- **A connection the car starts is accepted.** The board stopped being
  connectable ten seconds after boot, and the library ignored connection events
  while scanning, which the car reported as a failed connection.
- **Switching devices no longer needs the other one switched off.** Any
  disconnection made the library page the device just left, beating the one
  asked for. That now happens only at start-up, and once (was three times).
- **The Bluetooth screen is reachable while connected.** It handed straight
  back to the player for the rest of a session that began with Pair new.
- **Pair new discovers again** instead of quietly reconnecting to a remembered
  device that happened to be switched on nearby.

### Added
- **The last 5 connected devices are remembered** and listed, with the
  connected one marked. One tap moves between them, disconnecting first if
  need be; no pairing, and no waiting for a car to advertise.
- **A ✕ on each saved device deletes it**, after asking. Deleting drops the
  link, removes the pairing from the stack and stops the player reaching for it.
- Pairing is visible: a Secure Simple Pairing code the other device shows is
  displayed here too, and a refusal is reported instead of a silent wait.
- A connection attempt gives up after 20 s with "No answer — tap it to try
  again" rather than waiting indefinitely.
- The boot log prints the saved device's address beside its name, and every
  remembered device.

## 1.1.0 — 2026-09-18

A design system: the look is now data, the controls answer the finger while it
is down, and the release script can no longer ship the image that bricked a
board into upload mode.

### Looks
- **Style** is a new Setup row, separate from Theme: 7 shapes for buttons, rows
  and the progress bar — Classic (the 1.0.0 look, unchanged), Frosted deck,
  Hi-fi console, Swiss grid, Cassette pop, Terminal and Paper mono. Choosing a
  style also picks the theme it was designed against; Theme can still be
  changed afterwards.
- 3 new themes to go with them: Phosphor, Paper and Pop (8 in all).
- Setup rows show what a tap will do: a chevron opens something, a switch
  toggles, a track slides, a plain value cycles.

### Touch
- Buttons, rows, arrows and toggles light the moment a finger lands.
- Brightness is a slider: drag along it, 10–100 %.
- Volume: drag sideways on the Bluetooth / % indicator on the player.
- Panel type asks first, and says how to undo it, instead of "tap again".
- A drag never triggers touch calibration, however long it takes.

### Fixed
- `make_release.sh` checks that each app slot holds the right program. An image
  with the uploader in the player's slot passed the old check, and a board
  flashed with it could only boot into upload mode.
- `make_release.sh` keeps earlier releases' lines in `SHA256SUMS`, accepts
  `v1.1.0` as well as `1.1.0`, and refuses a version that does not match the
  firmware's own.

### Internals
- `tokens.h` (spacing, radii, icon sizes, row heights), `style.h` (the style
  table) and a component set in `ui.h` — rows, switch, slider, chip, pager,
  toggles, text buttons, toast and confirm dialog — replace the copies each
  screen used to draw for itself.

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
- 5 colour themes and 3 layouts (Classic, Big buttons, List), each in 4
  orientations; English / 繁體中文, brightness, colour inversion.
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
