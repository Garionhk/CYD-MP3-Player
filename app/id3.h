// ===========================================================================
// id3.h -- read title / artist / album from an MP3's ID3v2 tag
// ===========================================================================
// Handles ID3v2.2 (TT2/TP1/TAL), v2.3 and v2.4 (TIT2/TPE1/TALB), in all four
// text encodings (Latin-1, UTF-16 with BOM, UTF-16BE, UTF-8), always returning
// UTF-8. Embedded pictures and every other frame are skipped by seeking, so a
// tag carrying a 500 KB cover image costs a few small reads.
//
// ID3v1 is deliberately not read: Chinese files that only have a v1 tag almost
// always store it in GBK or Big5 with no marker saying which, and a wrong
// guess shows garbage where the file name would have been fine.
#pragma once

#include <Arduino.h>
#include <FS.h>

struct Id3Info {
  String title, artist, album;
};

// True if a v2 tag was found and at least one of the three fields was set.
bool id3_read(fs::File& f, Id3Info& out);
