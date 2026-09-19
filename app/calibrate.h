// ===========================================================================
// calibrate.h -- on-screen touch calibration wizard
// ===========================================================================
// The weather clock's wizard, measuring in the panel's NATIVE portrait
// orientation: it switches to rotation 0 for the duration, so the mapping it
// stores is independent of how the player is later rotated (touch.h).
//
// Four corner targets, axis mapping worked out by measurement, then a centre
// confirmation tap before anything is saved. Gives up after 60 s without a
// press and keeps the previous mapping, so an unattended boot never sticks.
#pragma once

// Returns true if a new calibration was accepted (and saved).
bool calibrate_run();
