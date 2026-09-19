// geometry.h -- the one rectangle type every screen and layout shares.
#pragma once

struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
  int cx() const { return x + w / 2; }
  int cy() const { return y + h / 2; }
};
