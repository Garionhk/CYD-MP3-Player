#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# sync_shared.sh -- push the canonical shared headers into every sketch folder.
#
# The Arduino IDE copies a sketch folder to a temp dir before building, so a
# sketch cannot #include "../../config/board.h". Each sketch folder therefore
# carries its own copy, and this script is what keeps those copies honest.
# Edit config/board.h, run this, rebuild.
#
#   ./tools/sync_shared.sh
#
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BOARD_SRC="config/board.h"

# Folders that need board.h (pins / board identity).
BOARD_TARGETS=(
  stage0/s01_display_touch_sd
  stage0/s02_bt_tone
  stage0/s03_mp3_sd_bt
  stage0/s04_gif_mp3_bt
  stage0/s05_bg_convert
  app
)

echo "syncing from $ROOT"

for dir in "${BOARD_TARGETS[@]}"; do
  if [ -d "$dir" ]; then
    cp "$BOARD_SRC" "$dir/board.h"
    echo "  board.h  -> $dir/"
  fi
done

echo "done."
