// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "tracklist.h"
#include "display.h"
#include "theme.h"
#include "ui.h"
#include "audio.h"
#include "storage.h"
#include "titles.h"

int tracklist_rows(const Rect& region, int rowH) { return max(1, region.h / rowH); }

int tracklist_clampTop(const Rect& region, int rowH, int top) {
  return constrain(top, 0, max(0, storage_trackCount() - tracklist_rows(region, rowH)));
}

int tracklist_topFor(const Rect& region, int rowH, int track) {
  return tracklist_clampTop(region, rowH, track - tracklist_rows(region, rowH) / 2 + 1);
}

void tracklist_drawRow(const Rect& region, int rowH, int top, int row) {
  const Palette& p = theme();
  const int track = top + row;
  const Rect r = { region.x, region.y + row * rowH, region.w, rowH };
  const bool playing = track == audio_currentTrack();
  const uint16_t bg = ui_rowSurface(r, row, playing);
  if (track >= storage_trackCount()) return;

  const uint16_t ink = playing ? p.onControlActive : p.text;
  ui_text(String(track + 1), r.x + 30, r.cy(), MR_DATUM, playing ? ink : p.dim, bg, 0,
          TYPE_CAPTION);
  ui_text(storage_trackLabel(track), r.x + 38, r.cy(), ML_DATUM, ink, bg, r.w - 44);
}

void tracklist_draw(const Rect& region, int rowH, int top) {
  for (int i = 0; i < tracklist_rows(region, rowH); i++) tracklist_drawRow(region, rowH, top, i);
  // Whatever is left under the last whole row.
  const int used = tracklist_rows(region, rowH) * rowH;
  if (used < region.h)
    tft.fillRect(region.x, region.y + used, region.w, region.h - used, theme().bg);
}

int tracklist_hit(const Rect& region, int rowH, int top, int x, int y) {
  if (!region.contains(x, y)) return -1;
  const int row = (y - region.y) / rowH;
  if (row >= tracklist_rows(region, rowH)) return -1;
  const int track = top + row;
  return track < storage_trackCount() ? track : -1;
}

#endif  // !CYD_UPLOADER
