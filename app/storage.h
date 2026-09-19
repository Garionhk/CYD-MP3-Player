// ===========================================================================
// storage.h -- SD card: mount, fixed folders, the music library index
// ===========================================================================
//   /music   MP3s, subfolders allowed (two levels deep)
//   /bg      background GIFs, bg1.gif, bg2.gif, ...
//   /.sys    files the firmware owns: library index, title strips, frames
//
// Names starting with '.' are skipped everywhere: a Mac writes "._name.mp3"
// next to every real file.
//
// The library lives in /.sys/library.idx, one line per track,
//     path \t title \t artist \n
// sorted by path, so folders group together. At boot /music is walked and a
// checksum of every path and size is compared with the one in the index
// header; only when they differ are the ID3 tags read again (the slow part:
// one file open per track). RAM holds just a 4-byte offset per track, so a
// thousand-song card costs 4 KB, not the ~70 KB a list of Strings would.
//
// All track lookups are safe from any task (the decode task and the UI both
// use them).
#pragma once

#include <Arduino.h>
#include <SD.h>

typedef void (*StorageProgressFn)(int done, int total);

bool storage_mount();                              // card + folders only; false if no card
bool storage_begin(StorageProgressFn progress);   // mount, then scan and index /music

int      storage_trackCount();
uint32_t storage_libraryCrc();         // changes whenever the track list does

String storage_trackPath(int i);
// What the screen shows: the ID3 title (plus " - artist" when there is one),
// or the file name without extension when the tag has no title.
String storage_trackLabel(int i);

// Read every entry in order with one pass over the index (boot-time jobs).
typedef void (*StorageEachFn)(int i, const String& path, const String& label);
void storage_forEach(StorageEachFn fn);
