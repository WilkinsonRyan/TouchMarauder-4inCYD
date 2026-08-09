//
// TouchBoard — BLE HID keyboard on an ESP32-2432S024C touchscreen.
//
// Views (tab bar at top): QWERTY keyboard / numpad / nav+arrows / Bluetooth.
// Pairs as a real hardware keyboard with any BLE HID host (phone, laptop, TV).
//
// Build: Arduino IDE, board "ESP32 Dev Module", Partition Scheme "Huge APP".
// Libraries: NimBLE-Arduino 2.x, LovyanGFX 1.2.x. See README.md.
//
#include "config.h"
#include "display.h"
#include "touch.h"
#include "hidkb.h"
#include "ui.h"

LGFX tft;

static bool bleOk = false;

// Onboard RGB LED (active LOW): blue blink = advertising, green = connected,
// solid red = BLE stack failed to start (check serial).
static void ledUpdate(uint32_t now) {
  if (!bleOk) {
    digitalWrite(PIN_LED_R, LOW);
    digitalWrite(PIN_LED_G, HIGH);
    digitalWrite(PIN_LED_B, HIGH);
    return;
  }
  bool conn = hidkb_connected();
  bool blink = (now / 500) % 2;
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, conn ? LOW : HIGH);
  digitalWrite(PIN_LED_B, (!conn && blink) ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, HIGH);
  digitalWrite(PIN_LED_B, HIGH);

  tft.init();
  tft.setRotation(2);        // portrait, 240x320 — USB-C down, matches Marauder
  tft.setBrightness(200);

  touch_begin();
  bleOk = hidkb_begin();   // UI still runs without BLE so you can debug
  ui_begin();

  Serial.println("[main] ready");
}

void loop() {
  static bool wasDown = false;
  uint32_t now = millis();

  TouchPoint p;
  bool isDown = touch_read(p);
  if (isDown && !wasDown) {
    Serial.printf("[touch] down at %d,%d\n", p.x, p.y);  // calibration aid
    ui_onTouchDown(p.x, p.y);
  } else if (!isDown && wasDown) {
    ui_onTouchUp();
  }
  wasDown = isDown;

  ui_tick(now);
  ledUpdate(now);
  delay(8);  // ~120 Hz touch poll; plenty for tapping, easy on the I2C bus
}
