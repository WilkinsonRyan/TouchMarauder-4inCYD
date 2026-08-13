//
// Touch layer — PORTED from the 2.4" CST820 capacitive (raw I2C) driver to the
// 4" board's XPT2046 resistive controller. The XPT2046 is set up on the LGFX
// device in display.h, so this file just polls LovyanGFX and hands back
// screen-space coordinates behind the same touch_begin()/touch_read() interface
// the rest of the sketch already uses.
//
// A small frame filter (N consecutive reads to press/release) rides out the
// jitter resistive panels produce; the UI's fire-on-release + T9_MIN_TAP_MS
// provide the rest of the ghost-touch defense.
//
#include <Arduino.h>
#include "config.h"
#include "touch.h"
#include "display.h"

static const uint8_t PRESS_FRAMES   = 2;   // reads in a row before "down"
static const uint8_t RELEASE_FRAMES = 2;   // empty reads in a row before "up"

void touch_begin() {
  // Touch is brought up by tft.init() in setup(); nothing to do here.
  Serial.println("[touch] XPT2046 (LovyanGFX) ready");
}

bool touch_read(TouchPoint& p) {
  static bool      stable_down = false;
  static uint8_t   down_count = 0, up_count = 0;
  static TouchPoint last = {0, 0};

  int32_t x = 0, y = 0;
  bool raw = tft.getTouch(&x, &y);   // already mapped to screen space + rotation

  if (raw) {
    up_count = 0;
    if (down_count < 255) down_count++;
    last.x = (int16_t)x;
    last.y = (int16_t)y;
    if (down_count >= PRESS_FRAMES) stable_down = true;
  } else {
    down_count = 0;
    if (up_count < 255) up_count++;
    if (up_count >= RELEASE_FRAMES) stable_down = false;
  }

  if (stable_down) p = last;
  return stable_down;
}
