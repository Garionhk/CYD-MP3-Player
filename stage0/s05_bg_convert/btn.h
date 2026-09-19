// Kept in a header: the Arduino builder hoists function prototypes above the
// sketch's own struct definitions, so a struct used as a parameter type must be
// declared before the .ino is scanned.
#pragma once
struct Btn { int x, y, w, h; const char* label; };
