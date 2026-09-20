#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# make_release.sh -- build both firmware images and merge them into ONE file
#
#   ./tools/make_release.sh 1.1.1
#
# Produces release/cyd-mp3-v1.1.1-4mb.bin and adds its line to
# release/SHA256SUMS, keeping the lines of earlier releases.
#
# The player (app0) and the WiFi uploader (app1) are separate images (see
# app/firmware.h). A person flashing a release should not have to know that,
# so the release is a full 4 MB image -- bootloader, partition table, OTA
# selector and BOTH apps at their offsets -- written with a single
#   esptool write-flash 0x0 cyd-mp3-v1.1.1-4mb.bin
#
# Like any full image written at 0x0 it is a factory reset: the gaps are 0xFF,
# which covers NVS (settings, calibration, saved speaker and WiFi). To update
# your own board and keep those, use ./tools/flash.sh app instead.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# "v1.1.1" and "V1.1.1" mean 1.1.1: the file name adds its own "v".
VERSION="${1:-}"
VERSION="${VERSION#[vV]}"
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "usage: tools/make_release.sh <version>   e.g. 1.1.1" >&2
  exit 1
fi
# The version the firmware reports (Setup > About, the boot banner) must be the
# one on the file.
if ! grep -q "#define FIRMWARE_VERSION \"$VERSION\"" app/firmware.h; then
  echo "app/firmware.h says $(grep -o 'FIRMWARE_VERSION "[^"]*"' app/firmware.h), not $VERSION" >&2
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

# Each slot must hold the right program. Checking only that the uploader is
# SOMEWHERE in the file is not enough: an image with the uploader in app0 and
# nothing in app1 passes that test, and a board flashed with it can only ever
# boot into upload mode -- whose "back to the player" lands on itself. That is
# a real failure this project shipped once.
#
# Each marker is a log line that exists in one program only. UI strings will
# not do: i18n.cpp is shared, so both programs carry them.
PLAYER_MARK="heap before audio + bt"      # app.ino, player half
UPLOADER_MARK="upload: hotspot"           # uploadmode.cpp
SLOT="$(mktemp)"
trap 'rm -f "$SLOT"' EXIT
check_slot() {   # <offset> <name> <must contain> <must not contain>
  # 0x1E0000 bytes: one app slot in min_spiffs.csv. No pipe into grep -q:
  # under pipefail its early exit would fail the pipeline even on a match.
  dd if="$OUT" of="$SLOT" bs=65536 skip=$(( $1 / 65536 )) count=30 2>/dev/null
  if ! grep -aqF "$3" "$SLOT" || grep -aqF "$4" "$SLOT"; then
    echo "sanity check failed: $2 at $(printf 0x%X "$1") is not the $2 image" >&2
    rm -f "$OUT"
    exit 1
  fi
}
check_slot 0x10000          player   "$PLAYER_MARK"   "$UPLOADER_MARK"
check_slot "$UPLOADER_OFFSET" uploader "$UPLOADER_MARK" "$PLAYER_MARK"

# One line per published release: replace this file's, keep the others.
NAME="$(basename "$OUT")"
touch release/SHA256SUMS
{ grep -vF "  $NAME" release/SHA256SUMS || true
  ( cd release && shasum -a 256 "$NAME" )
} > release/SHA256SUMS.new
mv release/SHA256SUMS.new release/SHA256SUMS
echo
ls -l "$OUT"
cat release/SHA256SUMS
