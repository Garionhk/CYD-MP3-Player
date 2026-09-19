#include "theme.h"
#include "settings.h"

// RGB565. Chosen on the panel, not a monitor: the ST7789 crushes very dark
// greys, so "dark" surfaces sit a step above black.
//
// Where two roles share a value it is deliberate: Paper and Pop draw their
// buttons as outlines on the screen colour, so btn == bg is the point, and
// Phosphor's framed buttons sit on the same surface as its panels.
static const Palette PALETTES[] = {
  { "Dark", "深色",
    /*bg*/ 0x0000, /*band*/ 0x10A2, /*surfaceRaised*/ 0x2986,
    /*text*/ 0xFFFF, /*dim*/ 0x9CD3, /*disabled*/ 0x52CB,
    /*outline*/ 0x4A6A, /*outlineSubtle*/ 0x1904,
    /*btn*/ 0x2124, /*btnText*/ 0xFFFF, /*btnActive*/ 0x0451, /*onControlActive*/ 0xFFFF, /*pressed*/ 0x3A29,
    /*accent*/ 0x07FF, /*onAccent*/ 0x0104,
    /*barBg*/ 0x39C7, /*bar*/ 0x07FF,
    /*good*/ 0x07E0, /*warn*/ 0xFEA0, /*bad*/ 0xF800,
  },

  { "Light", "淺色",
    /*bg*/ 0xF7BE, /*band*/ 0xE71C, /*surfaceRaised*/ 0xFFFF,
    /*text*/ 0x0000, /*dim*/ 0x6B4D, /*disabled*/ 0xAD55,
    /*outline*/ 0x9CD3, /*outlineSubtle*/ 0xCE79,
    /*btn*/ 0xD69A, /*btnText*/ 0x18E3, /*btnActive*/ 0x6D9F, /*onControlActive*/ 0x18E3, /*pressed*/ 0xBDB7,
    /*accent*/ 0x02B5, /*onAccent*/ 0xFFFF,
    /*barBg*/ 0xBDF7, /*bar*/ 0x02B5,
    /*good*/ 0x0400, /*warn*/ 0xC300, /*bad*/ 0xC000,
  },

  { "Neon", "霓虹",
    /*bg*/ 0x0806, /*band*/ 0x180B, /*surfaceRaised*/ 0x284F,
    /*text*/ 0xFFFF, /*dim*/ 0xB5BF, /*disabled*/ 0x6AD3,
    /*outline*/ 0x69DA, /*outlineSubtle*/ 0x3091,
    /*btn*/ 0x2011, /*btnText*/ 0x07FF, /*btnActive*/ 0x9012, /*onControlActive*/ 0x07FF, /*pressed*/ 0x3896,
    /*accent*/ 0xF81F, /*onAccent*/ 0x2804,
    /*barBg*/ 0x3013, /*bar*/ 0xF81F,
    /*good*/ 0x07F0, /*warn*/ 0xFFE0, /*bad*/ 0xF8A2,
  },

  { "Retro Amber", "復古琥珀",
    /*bg*/ 0x0800, /*band*/ 0x18C0, /*surfaceRaised*/ 0x3980,
    /*text*/ 0xFD20, /*dim*/ 0x9AA0, /*disabled*/ 0x6A40,
    /*outline*/ 0x7AC2, /*outlineSubtle*/ 0x2920,
    /*btn*/ 0x2920, /*btnText*/ 0xFD20, /*btnActive*/ 0x7A60, /*onControlActive*/ 0xFD20, /*pressed*/ 0x49E0,
    /*accent*/ 0xFEA0, /*onAccent*/ 0x20C0,
    /*barBg*/ 0x4200, /*bar*/ 0xFD20,
    /*good*/ 0xAFE0, /*warn*/ 0xFFE0, /*bad*/ 0xF800,
  },

  { "Ocean", "海洋",
    /*bg*/ 0x0109, /*band*/ 0x014C, /*surfaceRaised*/ 0x0A51,
    /*text*/ 0xEF7F, /*dim*/ 0x8D9C, /*disabled*/ 0x4B72,
    /*outline*/ 0x2B55, /*outlineSubtle*/ 0x09EE,
    /*btn*/ 0x0A2F, /*btnText*/ 0xEF7F, /*btnActive*/ 0x0418, /*onControlActive*/ 0xEF7F, /*pressed*/ 0x0AD3,
    /*accent*/ 0x3F1F, /*onAccent*/ 0x0125,
    /*barBg*/ 0x2B35, /*bar*/ 0x3F1F,
    /*good*/ 0x47EF, /*warn*/ 0xFEA0, /*bad*/ 0xFA49,
  },

  { "Phosphor", "螢光綠",
    /*bg*/ 0x0081, /*band*/ 0x00A1, /*surfaceRaised*/ 0x0922,
    /*text*/ 0x6F91, /*dim*/ 0x3CEB, /*disabled*/ 0x2AE7,
    /*outline*/ 0x1AC6, /*outlineSubtle*/ 0x11C3,
    /*btn*/ 0x00A1, /*btnText*/ 0x6F91, /*btnActive*/ 0x09E4, /*onControlActive*/ 0x9FF6, /*pressed*/ 0x1285,
    /*accent*/ 0x9FF6, /*onAccent*/ 0x0081,
    /*barBg*/ 0x11C3, /*bar*/ 0x6F91,
    /*good*/ 0x6F91, /*warn*/ 0xDF0B, /*bad*/ 0xFB4B,
  },

  { "Paper", "紙本",
    /*bg*/ 0xEF3B, /*band*/ 0xDEDA, /*surfaceRaised*/ 0xF79D,
    /*text*/ 0x1081, /*dim*/ 0x5ACA, /*disabled*/ 0x9CB1,
    /*outline*/ 0x1081, /*outlineSubtle*/ 0x8C4F,
    /*btn*/ 0xEF3B, /*btnText*/ 0x1081, /*btnActive*/ 0x1081, /*onControlActive*/ 0xEF3B, /*pressed*/ 0xCE37,
    /*accent*/ 0x1081, /*onAccent*/ 0xEF3B,
    /*barBg*/ 0xBDB5, /*bar*/ 0x1081,
    /*good*/ 0x2B47, /*warn*/ 0x8B42, /*bad*/ 0x9964,
  },

  { "Pop", "普普",
    /*bg*/ 0xF739, /*band*/ 0xFF11, /*surfaceRaised*/ 0xFFFF,
    /*text*/ 0x18C3, /*dim*/ 0x6B0A, /*disabled*/ 0xACF1,
    /*outline*/ 0x18C3, /*outlineSubtle*/ 0x18C3,
    /*btn*/ 0xFFFF, /*btnText*/ 0x18C3, /*btnActive*/ 0xF265, /*onControlActive*/ 0xFFFF, /*pressed*/ 0xFEC7,
    /*accent*/ 0xF265, /*onAccent*/ 0xFFFF,
    /*barBg*/ 0xFFFF, /*bar*/ 0xF265,
    /*good*/ 0x2D94, /*warn*/ 0xFD25, /*bad*/ 0xF265,
  },

};
static const int COUNT = sizeof(PALETTES) / sizeof(PALETTES[0]);

int theme_count() { return COUNT; }
const Palette& theme_at(int i) { return PALETTES[((i % COUNT) + COUNT) % COUNT]; }
const Palette& theme() { return theme_at(g_settings.theme); }
