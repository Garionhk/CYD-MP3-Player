// ===========================================================================
// firmware.h -- two firmwares on one board: the player and the uploader
// ===========================================================================
// WHY TWO. The WiFi stack reserves ~21 KB of static RAM in any image that
// links it, whether WiFi is ever started or not. Linking it into the player
// dropped the heap at Bluetooth connect to ~12 KB and the board reset in a
// loop (stage 5, docs/stage0_results.md). Bluetooth cannot give that back, so
// WiFi lives in a separate image:
//
//   app0 (ota_0)  PLAYER    Bluetooth, MP3 decoder, UI        -- no WiFi code
//   app1 (ota_1)  UPLOADER  WiFi, web file manager, same UI   -- no Bluetooth
//
// Both are built from this one sketch; CYD_UPLOADER selects the uploader
// (tools/build_all.sh and tools/flash.sh build and flash both). They share
// NVS -- settings, calibration, WiFi network -- and the SD card.
//
// Switching sets the boot partition and restarts. The uploader points boot
// back at the player as soon as it starts, so a crash or a power cut in
// upload mode always comes back up as the player.
#pragma once

#include <Arduino.h>

// One version for both images; tools/make_release.sh names the file after it.
#define FIRMWARE_VERSION "1.1.1"

// Is the other image flashed and valid? False when only the player was
// uploaded (e.g. with plain arduino-cli upload).
bool firmware_uploaderPresent();

// Restart into the other image. Return only on failure.
bool firmware_bootUploader();
bool firmware_bootPlayer();

// Make the NEXT boot the player without restarting now.
void firmware_nextBootPlayer();

// From inside the uploader: restart into the uploader once more.
bool firmware_bootUploaderAgain();

#ifdef CYD_UPLOADER
static const bool FIRMWARE_IS_UPLOADER = true;
#else
static const bool FIRMWARE_IS_UPLOADER = false;
#endif
