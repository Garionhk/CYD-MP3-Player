#include "style.h"
#include "settings.h"
#include "tokens.h"

// Row 0 is the look the player shipped with: it is the fallback when a new
// style turns out wrong on the panel, so its numbers must stay exactly what
// ui_button() used to hard-code -- radius 8, a 3 px inset, a filled face.
static const Style STYLES[] = {
  { "Classic", "經典",
    /*theme*/ 0,
    /*radius*/ RADIUS_ROUND, /*inset*/ 3, /*border*/ 0, /*shadow*/ 0,
    /*highlight*/ false, /*bevel*/ false,
    BTN_FILL, DIV_ZEBRA, BAR_SOLID,
    /*fonts*/ 2, 2, 2, /*framePx*/ 0 },

  // Surfaces stack instead of sitting flat: the face is the raised surface and
  // a lighter line along its top edge does the work a shadow would elsewhere.
  { "Frosted deck", "霧面",
    /*theme*/ 0,
    /*radius*/ RADIUS_PILL, /*inset*/ SP_S, /*border*/ 0, /*shadow*/ 0,
    /*highlight*/ true, /*bevel*/ false,
    BTN_PILL, DIV_HAIRLINE, BAR_KNOB,
    /*fonts*/ 2, 2, 2, /*framePx*/ 0 },

  // Moulded keys on a receiver front, with the time set in font 4 so it reads
  // as a counter. Font 7, the seven-segment face, was the obvious choice and
  // is unusable: it is 48 px tall and the roomiest time box in layouts.cpp is
  // 36, so it would spill over the progress bar in every skin.
  { "Hi-fi console", "音響",
    /*theme*/ 3,
    /*radius*/ RADIUS_SHARP + 2, /*inset*/ 3, /*border*/ 0, /*shadow*/ 0,
    /*highlight*/ false, /*bevel*/ true,
    BTN_BEVEL, DIV_ZEBRA, BAR_SEGMENT,
    /*fonts*/ 2, 4, 2, /*framePx*/ 0 },

  // No chrome at all: hairlines divide the touch cells and the accent block
  // marks the one control that is on. The cheapest of the seven to draw.
  { "Swiss grid", "方格",
    /*theme*/ 0,
    /*radius*/ RADIUS_SHARP, /*inset*/ 0, /*border*/ 0, /*shadow*/ 0,
    /*highlight*/ false, /*bevel*/ false,
    BTN_NONE, DIV_HAIRLINE, BAR_SOLID,
    /*fonts*/ 2, 2, 1, /*framePx*/ 0 },

  // Thick ink outlines over a hard offset block -- a sticker, not a gradient,
  // so it costs two extra fills per button and nothing else.
  { "Cassette pop", "卡帶",
    /*theme*/ 7,
    /*radius*/ RADIUS_SHARP, /*inset*/ 3, /*border*/ 3, /*shadow*/ 3,
    /*highlight*/ false, /*bevel*/ false,
    BTN_SHADOW, DIV_NONE, BAR_SOLID,
    /*fonts*/ 2, 2, 2, /*framePx*/ 0 },

  // Font 1 is the 8 px fixed-pitch GLCD face. framePx reserves the rule drawn
  // around the background area.
  { "Terminal", "終端機",
    /*theme*/ 5,
    /*radius*/ RADIUS_SHARP, /*inset*/ SP_XS, /*border*/ 1, /*shadow*/ 0,
    /*highlight*/ false, /*bevel*/ false,
    BTN_OUTLINE, DIV_BOX, BAR_SEGMENT,
    /*fonts*/ 1, 1, 1, /*framePx*/ SP_XS },

  // Three colours and heavy outlines: what survives a 10 % backlight, and the
  // one to reach for when a panel is inverted or miscalibrated.
  { "Paper mono", "紙本",
    /*theme*/ 6,
    /*radius*/ RADIUS_SOFT, /*inset*/ 3, /*border*/ 2, /*shadow*/ 0,
    /*highlight*/ false, /*bevel*/ false,
    BTN_OUTLINE, DIV_HAIRLINE, BAR_DOTS,
    /*fonts*/ 2, 2, 2, /*framePx*/ 0 },
};
static const int COUNT = sizeof(STYLES) / sizeof(STYLES[0]);

int style_count() { return COUNT; }
const Style& style_at(int i) { return STYLES[((i % COUNT) + COUNT) % COUNT]; }
const Style& style() { return style_at(g_settings.style); }
