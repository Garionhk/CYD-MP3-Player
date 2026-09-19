#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# make_release.sh -- build both firmware images and merge them into ONE file
#
#   ./tools/make_release.sh 1.0.0
#
# Produces release/cyd-mp3-v1.0.0-4mb.bin and updates release/SHA256SUMS.
#
# The player (app0) and the WiFi uploader (app1) are separate images (see
# app/firmware.h). A person flashing a release should not have to know that,
# so the release is a full 4 MB image -- bootloader, partition table, OTA
# selector and BOTH apps at their offsets -- written with a single
#   esptool write-flash 0x0 cyd-mp3-v1.0.0-4mb.bin
#
# Like any full image written at 0x0 it is a factory reset: the gaps are 0xFF,
# which covers NVS (settings, calibration, saved speaker and WiFi). To update
# your own board and keep those, use ./tools/flash.sh app instead.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
  echo "usage: tools/make_release.sh <version>   e.g. 1.0.0" >&2
  exit 1
fi

FQBN="esp32:esp32:esp32:PartitionScheme=min_spiffs"
UPLOADER_OFFSET=0x1F0000                    # app1 in min_spiffs.csv

CLI="$(command -v arduino-cli || echo "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")"
ESPTOOL="$(ls -d "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | tail -1)"
[ -x "$ESPTOOL" ] || ESPTOOL="$(command -v esptool || command -v esptool.py)"
BOOT_APP0="$(ls "$HOME"/Library/Arduino15/packages/esp32/hardware/esp32/*/tools/partitions/boot_app0.bin | tail -1)"

"$ROOT/tools/sync_shared.sh" >/dev/null

PLAYER="$ROOT/app/build/release-player"
UPLOADER="$ROOT/app/build/release-uploader"

echo "== player"
"$CLI" compile --fqbn "$FQBN" --clean --output-dir "$PLAYER" app | grep -E "Sketch uses|Global"
echo "== uploader"
"$CLI" compile --fqbn "$FQBN" --clean \
    --build-property "compiler.cpp.extra_flags=-DCYD_UPLOADER" \
    --build-property "compiler.c.extra_flags=-DCYD_UPLOADER" \
    --build-path "$ROOT/app/build/uploader-cache" --output-dir "$UPLOADER" app | grep -E "Sketch uses|Global"

mkdir -p release
OUT="release/cyd-mp3-v${VERSION}-4mb.bin"

# Offsets and flash parameters are the ones arduino-cli's own flash_args uses
# for this board and scheme (dio, 80m, 4MB; bootloader at 0x1000).
"$ESPTOOL" --chip esp32 merge-bin -o "$OUT" \
    --flash-mode dio --flash-freq 80m --flash-size 4MB --pad-to-size 4MB \
    0x1000  "$PLAYER/app.ino.bootloader.bin" \
    0x8000  "$PLAYER/app.ino.partitions.bin" \
    0xe000  "$BOOT_APP0" \
    0x10000 "$PLAYER/app.ino.bin" \
    "$UPLOADER_OFFSET" "$UPLOADER/app.ino.bin"

# The uploader image must really be in there: a merge that silently dropped
# app1 would produce a player-only release whose Setup says "uploader not
# flashed". This string exists only in uploadmode.cpp, and the UI strings do
# not work as a marker -- i18n.cpp is shared, so both halves carry those.
#
# grep -a, not `strings`: macOS `strings` scans object-file sections and finds
# nothing in a raw image.
#
# Nothing user-specific is in either image -- settings live in NVS, songs on the
# card -- so there is nothing else to scrub.
if ! grep -aq "upload: hotspot" "$OUT"; then
  echo "sanity check failed: the uploader image is not in $OUT" >&2
  exit 1
fi

( cd release && shasum -a 256 "$(basename "$OUT")" > SHA256SUMS )
echo
ls -l "$OUT"
cat release/SHA256SUMS
