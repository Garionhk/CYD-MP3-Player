// ===========================================================================
// audio.h -- MP3 playback engine: SD -> Helix -> 44.1 kHz stereo ring buffer
// ===========================================================================
// Proven in stage0/s03 (0 underruns over 4 minutes, clean audio):
//
//   decode task (core 1, prio 5)            Bluetooth task (core 0)
//   SD read -> Helix -> resample -> ring ==> audio_pull() -> A2DP -> speaker
//
// audio_pull() is the ONLY thing the Bluetooth side calls; it copies out of
// the ring buffer and never touches SD or the decoder. Everything else here
// is a request from the UI that the decode task acts on at its next chance,
// so no UI call ever blocks on the card.
//
// Position is counted in frames actually handed to Bluetooth, not frames
// decoded, so the time readout matches what is heard and stays right through
// pause and radio stalls.
#pragma once

#include <Arduino.h>

void audio_begin(int startTrack, uint32_t startMs, uint8_t volume, bool shuffle, uint8_t repeat);

// Call from loop(): moves on when a track ends, following shuffle and repeat.
void audio_tick();

// UI requests -- all return immediately. Call them from the loop task only
// (the play order they share is not locked).
void audio_playTrack(int index);
void audio_next();
void audio_prev();                  // restart, or previous in play order in the first 3 s
void audio_togglePause();
void audio_seekRelative(int seconds);
void audio_setVolume(uint8_t percent);
void audio_setShuffle(bool on);        // reshuffles with the current track first
void audio_setRepeat(uint8_t mode);    // RepeatMode from settings.h
bool    audio_shuffle();
uint8_t audio_repeat();

bool     audio_isPlaying();
int      audio_currentTrack();
uint8_t  audio_volume();
uint32_t audio_positionMs();
uint32_t audio_durationMs();        // 0 when it could not be worked out
// Increments whenever a different track (or the same one, restarted) opens.
uint32_t audio_trackSerial();

// Called by the Bluetooth data callback: fills `frames` stereo frames
// (interleaved int16 L,R). Always fills the whole buffer (silence if needed).
void audio_pull(int16_t* stereo, int32_t frames);

// Diagnostics for the serial stat line.
uint32_t audio_underruns();
uint32_t audio_takeMaxReadMs();     // slowest SD read since last call
int32_t  audio_maxRequest();       // largest frame count Bluetooth asked for at once
uint8_t  audio_bufferPct();
bool     audio_bufferLow();         // below half: background drawing backs off
uint32_t audio_decodeStackFree();
