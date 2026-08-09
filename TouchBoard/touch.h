#pragma once
#include <stdint.h>

struct TouchPoint {
  int16_t x;
  int16_t y;
};

void touch_begin();
// Returns true while a finger is down; fills p with screen-space coords.
bool touch_read(TouchPoint& p);
