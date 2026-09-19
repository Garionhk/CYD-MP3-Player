#include "ui.h"
#include "display.h"
#include "theme.h"
#include "titles.h"

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------
static void speaker(int cx, int cy, int s, uint16_t fg) {
  // Box plus cone, left of centre so a +/- fits on the right.
  const int bx = cx - s / 2, bh = s / 3;
  tft.fillRect(bx, cy - bh / 2, s / 5, bh, fg);
  tft.fillTriangle(bx + s / 5 - 1, cy - bh / 2, bx + s / 2, cy - s / 2,
                   bx + s / 5 - 1, cy + bh / 2, fg);
  tft.fillTriangle(bx + s / 5 - 1, cy + bh / 2, bx + s / 2, cy - s / 2,
                   bx + s / 2, cy + s / 2, fg);
}

void ui_icon(Icon icon, int cx, int cy, int s, uint16_t fg, uint16_t bg) {
  const int h = s / 2;
  switch (icon) {
    case ICON_PLAY:
      tft.fillTriangle(cx - h * 2 / 3, cy - h, cx - h * 2 / 3, cy + h, cx + h, cy, fg);
      break;
    case ICON_PAUSE:
      tft.fillRect(cx - h * 3 / 4, cy - h, h / 2 + 1, s, fg);
      tft.fillRect(cx + h / 4, cy - h, h / 2 + 1, s, fg);
      break;
    case ICON_NEXT:
      tft.fillTriangle(cx - h, cy - h, cx - h, cy + h, cx + h / 3, cy, fg);
      tft.fillTriangle(cx - h / 3, cy - h, cx - h / 3, cy + h, cx + h, cy, fg);
      tft.fillRect(cx + h - 1, cy - h, 3, s, fg);
      break;
    case ICON_PREV:
      tft.fillTriangle(cx + h, cy - h, cx + h, cy + h, cx - h / 3, cy, fg);
      tft.fillTriangle(cx + h / 3, cy - h, cx + h / 3, cy + h, cx - h, cy, fg);
      tft.fillRect(cx - h - 2, cy - h, 3, s, fg);
      break;
    case ICON_VOL_DOWN:
      speaker(cx - 2, cy, s, fg);
      tft.fillRect(cx + h / 2, cy - 1, h / 2 + 3, 3, fg);
      break;
    case ICON_VOL_UP:
      speaker(cx - 2, cy, s, fg);
      tft.fillRect(cx + h / 2, cy - 1, h / 2 + 3, 3, fg);
      tft.fillRect(cx + h / 2 + (h / 2 + 3) / 2 - 1, cy - (h / 2 + 3) / 2, 3, h / 2 + 3, fg);
      break;
    case ICON_GEAR: {
      const int r = h * 3 / 4;
      for (int i = 0; i < 8; i++) {
        const float a = i * PI / 4;
        const int tx = cx + cos(a) * r, ty = cy + sin(a) * r;
        tft.fillCircle(tx, ty, max(2, s / 9), fg);
      }
      tft.fillCircle(cx, cy, r, fg);
      tft.fillCircle(cx, cy, r / 2, bg);
      break;
    }
    case ICON_BLUETOOTH: {
      // The rune: a vertical stroke with two chevrons on the right.
      const int t = cy - h, b = cy + h, x0 = cx - h / 2, x1 = cx + h / 2;
      tft.drawLine(cx, t, cx, b, fg);
      tft.drawLine(cx, t, x1, cy - h / 2, fg);
      tft.drawLine(x1, cy - h / 2, x0, cy + h / 2, fg);
      tft.drawLine(cx, b, x1, cy + h / 2, fg);
      tft.drawLine(x1, cy + h / 2, x0, cy - h / 2, fg);
      break;
    }
    case ICON_BACK:
      tft.fillTriangle(cx - h, cy, cx, cy - h, cx, cy + h, fg);
      tft.fillRect(cx, cy - h / 3, h, h * 2 / 3, fg);
      break;
    case ICON_NOTE:
      tft.fillCircle(cx - h / 2, cy + h * 2 / 3, h / 3, fg);
      tft.fillCircle(cx + h * 2 / 3, cy + h / 2, h / 3, fg);
      tft.fillRect(cx - h / 2 + h / 3 - 2, cy - h, 3, h * 5 / 3, fg);
      tft.fillRect(cx + h * 2 / 3 + h / 3 - 2, cy - h * 7 / 6, 3, h * 5 / 3, fg);
      tft.fillTriangle(cx - h / 2 + h / 3 - 2, cy - h, cx + h, cy - h * 7 / 6 - 1,
                       cx + h, cy - h * 2 / 3, fg);
      tft.fillTriangle(cx - h / 2 + h / 3 - 2, cy - h, cx - h / 2 + h / 3 - 2, cy - h / 2,
                       cx + h, cy - h * 2 / 3, fg);
      break;
    case ICON_SHUFFLE: {
      // Two crossing paths, arrowheads on the right.
      const int l = cx - h, r = cx + h - 3, t = cy - h / 2, b = cy + h / 2;
      tft.drawLine(l, t, r, b, fg);   tft.drawLine(l, t + 1, r, b + 1, fg);
      tft.drawLine(l, b, r, t, fg);   tft.drawLine(l, b + 1, r, t + 1, fg);
      tft.fillTriangle(r + 3, t, r - 2, t - 4, r - 2, t + 5, fg);
      tft.fillTriangle(r + 3, b + 1, r - 2, b - 4, r - 2, b + 5, fg);
      break;
    }
    case ICON_REPEAT:
    case ICON_REPEAT_ONE: {
      const int w = s, hh = s * 2 / 3;
      tft.drawRoundRect(cx - w / 2, cy - hh / 2, w, hh, hh / 3, fg);
      tft.drawRoundRect(cx - w / 2 + 1, cy - hh / 2 + 1, w - 2, hh - 2, hh / 3, fg);
      // Break the top edge with an arrowhead pointing right.
      tft.fillRect(cx - 2, cy - hh / 2 - 1, 5, 4, bg);
      tft.fillTriangle(cx + 4, cy - hh / 2 + 1, cx - 2, cy - hh / 2 - 4, cx - 2, cy - hh / 2 + 6, fg);
      if (icon == ICON_REPEAT_ONE) {
        tft.setTextColor(fg, bg);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("1", cx, cy + 1, 1);
      }
      break;
    }
    case ICON_UP:
      tft.fillTriangle(cx, cy - h / 2, cx - h, cy + h / 2, cx + h, cy + h / 2, fg);
      break;
    case ICON_DOWN:
      tft.fillTriangle(cx, cy + h / 2, cx - h, cy - h / 2, cx + h, cy - h / 2, fg);
      break;
    case ICON_CHEVRON:
      // A thin arrowhead: "this row opens something".
      tft.fillTriangle(cx - h / 3, cy - h, cx + h / 2, cy, cx - h / 3, cy + h, fg);
      tft.fillTriangle(cx - h / 3 - 3, cy - h, cx + h / 2 - 3, cy, cx - h / 3 - 3, cy + h, bg);
      break;
    case ICON_LIST:
      for (int i = -1; i <= 1; i++) {
        tft.fillRect(cx - h, cy + i * h * 2 / 3 - 1, 3, 3, fg);
        tft.fillRect(cx - h + 6, cy + i * h * 2 / 3 - 1, s - 6, 3, fg);
      }
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Faces
// ---------------------------------------------------------------------------
// One draw for all seven looks (style.h). Every shape clears the touch
// rectangle to the screen colour first, so a control redrawn after a style
// change leaves nothing of the old shape behind.

// The face a control wears when it is merely sitting there. Outlined and
// chromeless styles wear whatever they sit on -- usually the screen, a
// dialog's panel inside one.
static uint16_t restFace(uint16_t surface) {
  const Palette& p = theme();
  switch (style().button) {
    case BTN_PILL:    return p.surfaceRaised;
    case BTN_OUTLINE:
    case BTN_NONE:    return surface;
    default:          return p.btn;
  }
}

// On an unfilled face the content has to read against the screen, not against
// a button colour that is not there.
static uint16_t faceInk(bool active, bool pressed) {
  const Palette& p = theme();
  const ButtonShape b = style().button;
  if (active || pressed) return p.onControlActive;
  return (b == BTN_OUTLINE || b == BTN_NONE) ? p.text : p.btnText;
}

// Paint the face and hand back the colour that landed, so the caller can draw
// an icon or a word against it.
static uint16_t ui_face(const Rect& r, bool active, bool pressed, uint16_t surface) {
  const Palette& p = theme();
  const Style&   s = style();
  const uint16_t face = pressed ? p.pressed : active ? p.btnActive : restFace(surface);

  tft.fillRect(r.x, r.y, r.w, r.h, surface);

  const int x = r.x + s.inset, y = r.y + s.inset;
  const int w = r.w - s.inset * 2, h = r.h - s.inset * 2;
  if (w <= 0 || h <= 0) return face;

  if (s.button == BTN_NONE) {
    // No face: a hairline along the top edge is all that separates one touch
    // cell from the next. An active cell still gets a solid block.
    if (active || pressed) tft.fillRect(r.x, r.y, r.w, r.h, face);
    else if (s.divider != DIV_NONE) tft.drawFastHLine(r.x, r.y, r.w, p.outlineSubtle);
    return face;
  }

  if (s.shadow) {
    // A hard offset block, not a gradient: one extra fill.
    if (s.radius) tft.fillRoundRect(x + s.shadow, y + s.shadow, w, h, s.radius, p.outline);
    else          tft.fillRect(x + s.shadow, y + s.shadow, w, h, p.outline);
  }

  if (s.radius) tft.fillRoundRect(x, y, w, h, s.radius, face);
  else          tft.fillRect(x, y, w, h, face);

  for (int i = 0; i < s.border; i++) {
    if (s.radius) tft.drawRoundRect(x + i, y + i, w - i * 2, h - i * 2, s.radius, p.outline);
    else          tft.drawRect(x + i, y + i, w - i * 2, h - i * 2, p.outline);
  }

  if (s.highlight && !active && !pressed)
    tft.drawFastHLine(x + s.radius / 2, y, w - s.radius, p.outline);

  if (s.bevel && !pressed) {
    // Light where the light would fall, dark where it would not.
    tft.drawFastHLine(x, y, w, active ? p.onControlActive : p.outline);
    tft.drawFastVLine(x, y, h, active ? p.onControlActive : p.outline);
    tft.drawFastHLine(x, y + h - 1, w, surface == p.bg ? p.bg : p.outlineSubtle);
    tft.drawFastVLine(x + w - 1, y, h, surface == p.bg ? p.bg : p.outlineSubtle);
  }
  return face;
}

void ui_button(const Rect& r, Icon icon, bool active, bool pressed) {
  const uint16_t face = ui_face(r, active, pressed, theme().bg);
  ui_icon(icon, r.cx(), r.cy(), min(r.w, r.h) * 9 / 20, faceInk(active, pressed), face);
}

void ui_textButton(const Rect& r, Txt label, bool primary, bool pressed, int32_t surface) {
  const uint16_t face = ui_face(r, primary, pressed, surface < 0 ? theme().bg : (uint16_t)surface);
  ui_label(label, r.cx(), r.cy(), MC_DATUM, faceInk(primary, pressed), face, r.w - SP_XL);
}

void ui_pager(const Rect& up, const Rect& down) {
  ui_button(up, ICON_UP);
  ui_button(down, ICON_DOWN);
}

// A toggle that lives on a header band rather than on the screen: off, it is
// nothing but its icon; on, the face appears under it.
void ui_iconToggle(const Rect& r, Icon icon, bool on, uint16_t surface, bool pressed) {
  const Palette& p = theme();
  const Style&   s = style();
  tft.fillRect(r.x, r.y, r.w, r.h, surface);
  const uint16_t face = pressed ? p.pressed : on ? p.btnActive : surface;
  const int rad = min<int>(s.radius, 6);
  if (rad) tft.fillRoundRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, rad, face);
  else     tft.fillRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, face);
  if (on && s.border)
    tft.drawRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, p.outline);
  ui_icon(icon, r.cx(), r.cy(), ICON_MD, (on || pressed) ? p.onControlActive : p.dim, face);
}

void ui_chip(const Rect& r, Icon icon, uint16_t iconColour, const String& text,
             uint16_t surface) {
  const Palette& p = theme();
  tft.fillRect(r.x, r.y, r.w, r.h, surface);
  ui_icon(icon, r.x + ICON_SM / 2 + 1, r.cy(), ICON_SM, iconColour, surface);
  ui_text(text, r.x + r.w - SP_XS, r.cy(), MR_DATUM, p.dim, surface,
          r.w - ICON_SM - SP_M, TYPE_CAPTION);
}

void ui_switch(const Rect& r, bool on) {
  const Palette& p = theme();
  const int h = min(r.h - SP_M, ICON_MD), w = h * 9 / 5;
  if (h < 8) return;
  const int x = r.x + r.w - SP_L - w;
  tft.fillRoundRect(x, r.cy() - h / 2, w, h, h / 2, on ? p.accent : p.barBg);
  if (style().border)
    tft.drawRoundRect(x, r.cy() - h / 2, w, h, h / 2, p.outline);
  tft.fillCircle(on ? x + w - h / 2 : x + h / 2, r.cy(), h / 2 - 2, on ? p.onAccent : p.dim);
}

void ui_slider(const Rect& r, int value, int maxValue, uint16_t surface) {
  const int h = constrain(r.h / 4, 4, 8);
  const Rect track = { r.x, r.cy() - h / 2, r.w, h };
  const int px = maxValue > 0 ? (int)((int32_t)r.w * constrain(value, 0, maxValue) / maxValue) : 0;
  ui_bar(track, px, surface);
}

// ---------------------------------------------------------------------------
// List rows
// ---------------------------------------------------------------------------
static uint16_t rowColour(int index, bool active, bool pressed) {
  const Palette& p = theme();
  if (pressed) return p.pressed;
  if (active)  return p.btnActive;
  return (style().divider == DIV_ZEBRA && (index % 2) == 0) ? p.band : p.bg;
}

// Paint `part` of `row` -- the whole row, or a slice of it -- with the row's
// colour and whatever pieces of its divider fall inside the slice. A box's
// side edges belong only to the slices at the row's ends.
static void paintRowPart(const Rect& row, const Rect& part, uint16_t bg, bool plain) {
  const Palette& p = theme();
  tft.fillRect(part.x, part.y, part.w, part.h, bg);
  if (plain) return;
  switch (style().divider) {
    case DIV_HAIRLINE:
      tft.drawFastHLine(part.x, row.y + row.h - 1, part.w, p.outlineSubtle);
      break;
    case DIV_BOX:
      tft.drawFastHLine(part.x, row.y, part.w, p.outlineSubtle);
      tft.drawFastHLine(part.x, row.y + row.h - 1, part.w, p.outlineSubtle);
      if (part.x == row.x) tft.drawFastVLine(row.x, row.y, row.h, p.outlineSubtle);
      if (part.x + part.w == row.x + row.w)
        tft.drawFastVLine(row.x + row.w - 1, row.y, row.h, p.outlineSubtle);
      break;
    default:
      break;
  }
}

uint16_t ui_rowSurface(const Rect& r, int index, bool active, bool pressed) {
  const uint16_t bg = rowColour(index, active, pressed);
  paintRowPart(r, r, bg, active || pressed);
  return bg;
}

// Room kept at the right of a row for a value like "100%".
static const int ROW_VALUE_W = 38;

Rect ui_rowTrack(const Rect& r) {
  const int left  = r.x + r.w / 2;                       // the label is capped there
  const int right = r.x + r.w - SP_L - ROW_VALUE_W;
  return { left, r.y, max(0, right - left), r.h };
}

RowValue ui_nav(const String& text)   { return { RK_NAV,    T_COUNT, text,   0, 0 }; }
RowValue ui_cycle(Txt label)          { return { RK_CYCLE,  label,   String(), 0, 0 }; }
RowValue ui_cycle(const String& text) { return { RK_CYCLE,  T_COUNT, text,   0, 0 }; }
RowValue ui_info(const String& text)  { return { RK_INFO,   T_COUNT, text,   0, 0 }; }
RowValue ui_toggleValue(bool on)      { return { RK_TOGGLE, T_COUNT, String(), on ? 1 : 0, 1 }; }
RowValue ui_sliderValue(int v, int m) { return { RK_SLIDER, T_COUNT, String(), v, m }; }

// A slider row's value and track, drawn on `bg`. Shared by ui_row and
// ui_rowSlider so a drag and a full redraw cannot disagree.
static void drawSliderValue(const Rect& r, uint16_t bg, int value, int maxValue) {
  const Palette& p = theme();
  ui_text(String(value) + "%", r.x + r.w - SP_L, r.cy(), MR_DATUM, p.accent, bg, ROW_VALUE_W,
          TYPE_CAPTION);
  const Rect track = ui_rowTrack(r);
  if (track.w > SP_XXL) ui_slider(track, value, maxValue, bg);
}

void ui_rowSlider(const Rect& r, int index, int value, int maxValue) {
  const uint16_t bg = rowColour(index, false, false);
  // From just left of the track (the knob overhangs it) to the row's end.
  const int x = ui_rowTrack(r).x - SP_M;
  paintRowPart(r, { x, r.y, r.x + r.w - x, r.h }, bg, false);
  drawSliderValue(r, bg, value, maxValue);
}

void ui_row(const Rect& r, int index, Txt label, const RowValue& v, bool pressed) {
  const Palette& p = theme();
  const uint16_t bg = ui_rowSurface(r, index, false, pressed);
  // Capped short of the middle, where a slider's track (and its knob) begin.
  const int labelW = ui_label(label, r.x + SP_L, r.cy(), ML_DATUM, p.text, bg,
                              r.w / 2 - SP_L - SP_M);
  const uint16_t col = (v.kind == RK_INFO) ? p.dim : p.accent;

  // Where the value may be drawn: right of the label, left of its affordance.
  int right = r.x + r.w - SP_L;
  if (v.kind == RK_NAV) right -= ICON_SM;
  int room = right - (r.x + SP_L + labelW + SP_M);
  if (room < 0) room = 0;

  switch (v.kind) {
    case RK_TOGGLE:
      ui_switch(r, v.value != 0);
      break;
    case RK_SLIDER:
      drawSliderValue(r, bg, v.value, v.max);
      break;
    case RK_NAV:
      if (v.text.length() && room > 0)
        ui_text(v.text, right - SP_S, r.cy(), MR_DATUM, col, bg, room, TYPE_CAPTION);
      ui_icon(ICON_CHEVRON, r.x + r.w - SP_L - ICON_SM / 2, r.cy(), ICON_SM, p.dim, bg);
      break;
    default:
      if (room <= 0) break;
      if (v.label != T_COUNT) ui_label(v.label, right, r.cy(), MR_DATUM, col, bg, room);
      else                    ui_text(v.text, right, r.cy(), MR_DATUM, col, bg, room);
      break;
  }
}

// The progress track, in whatever form the style asks for. `px` is how much of
// `r.w` has played.
void ui_bar(const Rect& r, int px, uint16_t surface) {
  const Palette& p = theme();
  const Style&   s = style();
  px = constrain(px, 0, r.w);

  switch (s.bar) {
    case BAR_SEGMENT: {
      // Discrete cells with a 1 px gutter, lit up to the play head.
      const int cell = max(4, r.h * 2);
      for (int x = 0; x + cell <= r.w; x += cell)
        tft.fillRect(r.x + x, r.y, cell - 1, r.h, x + cell <= px ? p.bar : p.barBg);
      // Whatever does not divide evenly, left as track.
      const int used = (r.w / cell) * cell;
      if (used < r.w) tft.fillRect(r.x + used, r.y, r.w - used, r.h, p.barBg);
      break;
    }
    case BAR_DOTS: {
      const int step = max(4, r.h + 3);
      tft.fillRect(r.x, r.y, r.w, r.h, surface);
      for (int x = 0; x + r.h <= r.w; x += step)
        tft.fillRect(r.x + x, r.y, r.h, r.h, x < px ? p.bar : p.barBg);
      break;
    }
    case BAR_KNOB:
      // The handle overhangs the track, so the strip it sweeps has to be wiped
      // or it leaves a trail behind it.
      tft.fillRect(r.x, r.cy() - r.h, r.w, r.h * 2 + 1, surface);
      tft.fillRect(r.x, r.y, px, r.h, p.bar);
      tft.fillRect(r.x + px, r.y, r.w - px, r.h, p.barBg);
      tft.fillCircle(r.x + constrain(px, r.h, r.w - r.h), r.cy(), r.h, p.text);
      break;
    default:
      tft.fillRect(r.x, r.y, px, r.h, p.bar);
      tft.fillRect(r.x + px, r.y, r.w - px, r.h, p.barBg);
      break;
  }
}

uint8_t ui_font(TypeRole role) {
  const Style& st = style();
  return role == TYPE_NUMERIC ? st.fontNumeric : role == TYPE_CAPTION ? st.fontCaption : st.fontBody;
}

uint8_t ui_numericFont(const String& s, int maxHeight) {
  const uint8_t f = ui_font(TYPE_NUMERIC);
  const uint8_t body = ui_font(TYPE_BODY);
  if (f == body) return f;
  if (maxHeight > 0 && tft.fontHeight(f) > maxHeight) return body;
  if (f == 7) {
    for (unsigned i = 0; i < s.length(); i++) {
      const char c = s[i];
      if (!(c >= '0' && c <= '9') && c != ':' && c != '.' && c != '-' && c != ' ') return body;
    }
  }
  return f;
}

static bool isAscii(const String& s) {
  for (unsigned i = 0; i < s.length(); i++)
    if ((uint8_t)s[i] >= 0x80) return false;
  return true;
}

int ui_text(const String& text, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg, int maxW,
            TypeRole role) {
  const int hAlign = datum % 3;              // 0 left, 1 centre, 2 right
  const int vAlign = datum / 3;              // 0 top, 1 middle (baseline datums unused)
  const uint8_t f = ui_font(role);
  if (!isAscii(text) && titles_load(text)) {
    int w = titles_width();
    if (maxW > 0) w = min(w, maxW);
    const int left = x - (hAlign == 1 ? w / 2 : hAlign == 2 ? w : 0);
    const int top = y - (vAlign == 1 ? TITLE_STRIP_H / 2 : 0);
    titles_draw({ left, top, w, TITLE_STRIP_H }, 0, 0, fg, bg);
    return w;
  }
  tft.setTextColor(fg, bg);
  tft.setTextDatum(datum);
  const int fh = tft.fontHeight(f);
  if (maxW > 0 && tft.textWidth(text, f) > maxW) {
    const int left = x - (hAlign == 1 ? maxW / 2 : hAlign == 2 ? maxW : 0);
    const int top = y - (vAlign == 1 ? fh / 2 : 0);
    tft.setViewport(left, top, maxW, fh);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(text, 0, 0, f);
    tft.resetViewport();
    return maxW;
  }
  tft.drawString(text, x, y, f);
  return tft.textWidth(text, f);
}

int ui_label(Txt id, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg, int maxW,
             TypeRole role) {
  const String s = tr(id);
  // Chinese without its strip would draw as nothing: say it in English instead.
  if (!isAscii(s) && !titles_load(s)) return ui_text(tr_en(id), x, y, datum, fg, bg, maxW, role);
  return ui_text(s, x, y, datum, fg, bg, maxW, role);
}

void ui_header(Txt title, const String& suffix) {
  const Palette& p = theme();
  tft.fillRect(0, 0, tft.width(), UI_HEADER_H, p.band);
  ui_icon(ICON_BACK, 20, UI_HEADER_H / 2, 14, p.text, p.band);
  const int w = ui_label(title, 44, UI_HEADER_H / 2, ML_DATUM, p.text, p.band);
  if (suffix.length()) ui_text(suffix, 44 + w + 8, UI_HEADER_H / 2, ML_DATUM, p.dim, p.band);
}

String ui_time(uint32_t ms, bool known) {
  if (!known) return "--:--";
  const uint32_t s = ms / 1000;
  char b[16];
  if (s >= 3600) snprintf(b, sizeof(b), "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
  else           snprintf(b, sizeof(b), "%u:%02u", s / 60, s % 60);
  return b;
}

#ifndef CYD_UPLOADER
#include "background.h"

// ---------------------------------------------------------------------------
// Screen switcher
// ---------------------------------------------------------------------------
static const Screen* SCREENS[SCR_COUNT] = { &SCREEN_PLAYER, &SCREEN_SETUP, &SCREEN_BLUETOOTH,
                                            &SCREEN_LIBRARY };
static ScreenId current = SCR_PLAYER;
static bool     entered = false;

enum Overlay : uint8_t { OV_NONE, OV_TOAST, OV_CONFIRM };
static Overlay  overlay = OV_NONE;

// The finger, as the press hooks see it (Screen::press).
static bool pressDown     = false;
static bool pressClaimed  = false;   // a hook took it as a drag: swallow its event
static bool pressOrphaned = false;   // the screen changed under it: ignore it
static int  pressX = 0, pressY = 0;

void ui_go(ScreenId id) {
  if (entered && SCREENS[current]->leave) SCREENS[current]->leave();
  // A screen can change from a tick (Bluetooth moves on once the speaker
  // pairs) with a finger still down. That press belonged to the old screen;
  // the new one never saw it land, so it hears nothing of it.
  if (pressDown) pressOrphaned = true;
  overlay = OV_NONE;                     // the new screen draws over it anyway
  current = id;
  entered = true;
  SCREENS[current]->enter();
}

ScreenId ui_current() { return current; }
void ui_redraw()      { SCREENS[current]->enter(); }

// ---------------------------------------------------------------------------
// Confirm dialog
// ---------------------------------------------------------------------------
static ConfirmFn confirmFn = nullptr;
static Txt       confirmOk = T_RESTART;
static Rect      cancelRect, okRect;

static void drawConfirmButtons(int pressed) {
  const uint16_t panel = theme().surfaceRaised;
  ui_textButton(cancelRect, T_CANCEL, false, pressed == 0, panel);
  ui_textButton(okRect, confirmOk, true, pressed == 1, panel);
}

void ui_confirm(Txt title, Txt line1, Txt line2, Txt okLabel, ConfirmFn onOk) {
  const Palette& p = theme();
  const Style&   s = style();
  bg_stop();                             // its masks cannot cover a dialog
  overlay   = OV_CONFIRM;
  confirmFn = onOk;
  confirmOk = okLabel;

  const int W = tft.width(), H = tft.height();
  const int w = min(W - SP_L * 2, 296), h = 150;
  const Rect r = { (W - w) / 2, (H - h) / 2, w, h };
  const int rad = s.radius;

  if (s.shadow) {
    if (rad) tft.fillRoundRect(r.x + s.shadow, r.y + s.shadow, r.w, r.h, rad, p.outline);
    else     tft.fillRect(r.x + s.shadow, r.y + s.shadow, r.w, r.h, p.outline);
  }
  if (rad) tft.fillRoundRect(r.x, r.y, r.w, r.h, rad, p.surfaceRaised);
  else     tft.fillRect(r.x, r.y, r.w, r.h, p.surfaceRaised);
  // Always at least a hairline: a panel must not melt into the screen behind.
  for (int i = 0; i < max<int>(1, s.border); i++) {
    if (rad) tft.drawRoundRect(r.x + i, r.y + i, r.w - i * 2, r.h - i * 2, rad, p.outline);
    else     tft.drawRect(r.x + i, r.y + i, r.w - i * 2, r.h - i * 2, p.outline);
  }

  const int tx = r.x + SP_L, tw = r.w - SP_L * 2;
  ui_label(title, tx, r.y + 22, ML_DATUM, p.text, p.surfaceRaised, tw);
  ui_label(line1, tx, r.y + 52, ML_DATUM, p.dim, p.surfaceRaised, tw);
  ui_label(line2, tx, r.y + 74, ML_DATUM, p.dim, p.surfaceRaised, tw);

  const int bh = 44, by = r.y + r.h - SP_M - bh, bw = (r.w - SP_M * 3) / 2;
  cancelRect = { r.x + SP_M, by, bw, bh };
  okRect     = { r.x + SP_M * 2 + bw, by, bw, bh };
  drawConfirmButtons(-1);
}

static void closeOverlay() {
  overlay = OV_NONE;
  ui_redraw();
}

static void confirmTouch(TouchEvent ev, int x, int y) {
  if (ev != TOUCH_TAP) return;
  const bool ok = okRect.contains(x, y);
  if (!ok && !cancelRect.contains(x, y)) return;    // a stray tap is not an answer
  const ConfirmFn fn = ok ? confirmFn : nullptr;
  confirmFn = nullptr;
  overlay = OV_NONE;
  if (fn) fn();                          // may not return: the panel switch restarts
  ui_redraw();
}

static void confirmPress(PressPhase phase, int x, int y) {
  static int lit = -1;
  if (phase == PRESS_DOWN) {
    lit = cancelRect.contains(x, y) ? 0 : okRect.contains(x, y) ? 1 : -1;
    if (lit >= 0) drawConfirmButtons(lit);
  } else if (phase == PRESS_UP && lit >= 0) {
    lit = -1;
    drawConfirmButtons(-1);
  }
}

// ---------------------------------------------------------------------------
// Toast
// ---------------------------------------------------------------------------
static uint32_t toastUntil = 0;

void ui_toast(Txt text, uint32_t ms) {
  const Palette& p = theme();
  const Style&   s = style();
  const int W = tft.width(), H = tft.height();
  const Rect r = { SP_XL, H - SP_XL - 34, W - SP_XL * 2, 34 };
  // Inverted -- text colour as the face -- so it stands off any row under it.
  if (s.shadow) tft.fillRect(r.x + s.shadow, r.y + s.shadow, r.w, r.h, p.outline);
  if (s.radius) tft.fillRoundRect(r.x, r.y, r.w, r.h, s.radius, p.text);
  else          tft.fillRect(r.x, r.y, r.w, r.h, p.text);
  ui_label(text, r.cx(), r.cy(), MC_DATUM, p.bg, p.text, r.w - SP_XL);
  overlay = OV_TOAST;
  toastUntil = millis() + ms;
}

// ---------------------------------------------------------------------------
// The finger, between landing and release
// ---------------------------------------------------------------------------
static bool dispatchPress(PressPhase phase, int x, int y) {
  if (pressOrphaned || overlay == OV_TOAST) return false;
  if (overlay == OV_CONFIRM) { confirmPress(phase, x, y); return false; }
  const Screen* s = SCREENS[current];
  return s->press ? s->press(phase, x, y) : false;
}

// Bring the press hooks up to date with the finger. Returns true when a press
// has just ended that must not produce an event (a drag, or an orphan).
static bool syncPress() {
  if (touch_isDown()) {
    int x, y;
    if (!pressDown) {
      pressDown = true;
      pressClaimed = pressOrphaned = false;
      touch_position(x, y);              // where it landed: what a tap acts on
      pressX = x;
      pressY = y;
      pressClaimed |= dispatchPress(PRESS_DOWN, x, y);
    } else {
      touch_livePosition(x, y);
      if (x != pressX || y != pressY) {
        pressX = x;
        pressY = y;
        pressClaimed |= dispatchPress(PRESS_MOVE, x, y);
      }
    }
    return false;
  }
  if (!pressDown) return false;
  pressDown = false;
  dispatchPress(PRESS_UP, pressX, pressY);
  const bool swallow = pressClaimed || pressOrphaned;
  pressClaimed = pressOrphaned = false;
  return swallow;
}

bool ui_pressClaimed() { return pressDown && pressClaimed; }

void ui_tick(uint32_t now) {
  if (!entered) return;
  // touch_poll() ran first this loop. If it saw a release that made an event,
  // ui_touch() has already synced; here we catch releases that made none.
  syncPress();
  if (overlay == OV_TOAST && (int32_t)(now - toastUntil) >= 0) closeOverlay();
  if (overlay != OV_NONE) return;        // nothing may paint over an overlay
  if (SCREENS[current]->tick) SCREENS[current]->tick(now);
}

void ui_touch(TouchEvent ev, int x, int y) {
  if (!entered) return;
  // Restore whatever the press lit BEFORE the event runs: the event may move
  // to another screen, which must not then be drawn over.
  if (syncPress()) return;
  if (overlay == OV_TOAST)   { closeOverlay(); return; }   // a tap dismisses it
  if (overlay == OV_CONFIRM) { confirmTouch(ev, x, y); return; }
  if (SCREENS[current]->touch) SCREENS[current]->touch(ev, x, y);
}

#endif  // !CYD_UPLOADER
