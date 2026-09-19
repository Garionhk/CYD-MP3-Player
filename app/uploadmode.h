// ===========================================================================
// uploadmode.h -- WiFi file manager: add songs, backgrounds and the title font
// ===========================================================================
// A separate FIRMWARE image, not a screen (firmware.h explains why): WiFi and
// Bluetooth never share a heap, or even a binary. "Upload music" in Setup
// restarts into this image; "Done" restarts back into the player, which
// re-indexes the library, renders new titles and converts new backgrounds on
// its way up.
//
// Networking, the same arrangement as the weather clock's setup portal:
//   - saved home WiFi joins as a station; the screen shows its address
//   - no saved WiFi, or joining fails: the player raises its own open hotspot
//     "CYD-MP3-XXXX" with a captive portal, so a phone that joins it opens the
//     page by itself. Uploading works over the hotspot too -- no home network
//     needed at all -- and the page's WiFi tab saves a home network.
//
// The page has no password. Anyone on the same network (or near enough to
// join the hotspot) while upload mode is on can manage the card's files --
// the same trade the weather clock makes for its settings page. It is only
// reachable while the owner has deliberately put the player in this mode.
#pragma once

#include <Arduino.h>

void uploadmode_run();                 // the uploader image's whole life (does not return)
