#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build_all.sh -- compile every Stage 0 sketch (and the app) in one go.
#
# Catches the whole class of "it didn't build" problems -- a stale User_Setup.h,
# a missing library, a header that never got synced -- before you plug the
# board in. It does NOT flash anything; use tools/flash.sh for that.
#
#   ./tools/build_all.sh            # everything
#   ./tools/build_all.sh stage0     # just the Stage 0 tests
#   ./tools/build_all.sh stage0/s02_touch_test
#
# Uses the arduino-cli bundled inside Arduino IDE 2 if one isn't on PATH.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# min_spiffs: two 1.9 MB app slots. The app is TWO firmware images (see
# app/firmware.h) -- the player in app0 and the WiFi uploader in app1 -- because
# linking WiFi into the player cost the Bluetooth link ~21 KB of heap. NVS stays
# at 0x9000/0x5000, so settings and calibration survive the change of scheme.
# Stage 0 sketches build with it too; each fits one slot.
FQBN="${FQBN:-esp32:esp32:esp32:PartitionScheme=min_spiffs}"

if command -v arduino-cli >/dev/null 2>&1; then
  CLI="$(command -v arduino-cli)"
elif [ -x "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" ]; then
  CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
else
  echo "arduino-cli not found (not on PATH, not inside Arduino IDE.app)." >&2
  exit 1
fi

FILTER="${1:-}"

# Every folder holding a .ino, in test order.
SKETCHES=(
  stage0/s01_display_touch_sd
  stage0/s02_bt_tone
  stage0/s03_mp3_sd_bt
  stage0/s04_gif_mp3_bt
  stage0/s05_bg_convert
  app
)

"$ROOT/tools/sync_shared.sh" >/dev/null

echo "arduino-cli: $CLI"
echo "fqbn       : $FQBN"
echo

pass=0
fail=0
failed_list=()

for sketch in "${SKETCHES[@]}"; do
  [ -d "$sketch" ] || continue
  if [ -n "$FILTER" ] && [[ "$sketch" != "$FILTER"* ]]; then continue; fi

  printf '%-34s ' "$sketch"
  out="$("$CLI" compile --fqbn "$FQBN" --warnings none "$sketch" 2>&1)"
  # The app also builds its uploader image, into its own folder and cache.
  if [ $? -eq 0 ] && [ "$sketch" = "app" ]; then
    echo "OK    $(echo "$out" | grep -m1 'Sketch uses' | sed 's/Sketch uses //; s/ bytes.*(\([0-9]*%\)).*/ B flash (\1)/')  [player]"
    printf '%-34s ' "app (uploader)"
    out="$("$CLI" compile --fqbn "$FQBN" --warnings none \
            --build-property "compiler.cpp.extra_flags=-DCYD_UPLOADER" \
            --build-property "compiler.c.extra_flags=-DCYD_UPLOADER" \
            --build-path "$ROOT/app/build/uploader-cache" "$sketch" 2>&1)"
  fi
  if [ $? -eq 0 ]; then
    # Pull the flash/RAM line out of the noise so regressions in size show up.
    usage="$(echo "$out" | grep -m1 'Sketch uses' | sed 's/Sketch uses //; s/ bytes.*(\([0-9]*%\)).*/ B flash (\1)/')"
    echo "OK    $usage"
    pass=$((pass + 1))
  else
    echo "FAIL"
    echo "$out" | grep -E 'error:|#error|#warning' | head -8 | sed 's/^/    /'
    fail=$((fail + 1))
    failed_list+=("$sketch")
  fi
done

echo
echo "$pass built, $fail failed"
if [ "$fail" -gt 0 ]; then
  printf '  failed: %s\n' "${failed_list[@]}"
  exit 1
fi
