//
// Minimal CST820 capacitive touch driver (also reads CST816-family chips).
// ~60 lines of raw I2C beats a library dependency for a part this simple,
// and you can see exactly what the chip is reporting.
//
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "touch.h"

static const uint8_t CST_ADDR = 0x15;

static bool readRegs(uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(CST_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)CST_ADDR, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(CST_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static void IRAM_ATTR touchIsr();

void touch_begin() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(20);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(150);  // chip boot time after reset

  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);

  // 0xFE = DisAutoSleep: the CST820 powers itself down after a few seconds
  // idle and stops answering I2C. A keyboard must never do that.
  writeReg(0xFE, 0xFF);

  // 0xFA = IrqCtl: EnTouch (pulse while touched) | EnChange (pulse on
  // change). Don't rely on the chip's default IRQ config to pulse at all.
  writeReg(0xFA, 0x60);

  pinMode(PIN_TOUCH_INT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_INT), touchIsr, FALLING);

  uint8_t chipId = 0;
  if (readRegs(0xA7, &chipId, 1) && chipId != 0x00) {
    Serial.printf("[touch] CST8xx found, chip ID 0x%02X\n", chipId);
  } else {
    Serial.println("[touch] WARNING: no CST8xx touch controller at 0x15.");
    Serial.println("[touch] If this is the resistive (R) board variant, this");
    Serial.println("[touch] driver won't work — it needs XPT2046 over SPI.");
  }
}

// The CST820 occasionally emits garbage frames (RF noise from the BLE radio,
// dirty USB power), and a single bad frame must not become a keystroke.
// So raw frames pass through a stability filter: a press needs PRESS_FRAMES
// consecutive valid touches, a release needs RELEASE_FRAMES consecutive
// empty reads, and frames with impossible values are dropped entirely.
static const uint8_t PRESS_FRAMES   = 3;  // ~24ms at the 8ms poll rate
static const uint8_t RELEASE_FRAMES = 3;  // ~24ms; also rides out I2C hiccups
static const int     JITTER_PX      = 40; // noise jumps around; fingers don't

// The CST820 pulses its INT line every time it produces a genuine touch
// report. Register data read without a recent pulse is stale or noise.
// The gate FAILS OPEN: it only arms after the first real pulse is seen, so
// a dead/miswired INT line degrades to unfiltered touch, never to no touch.
static volatile uint32_t s_lastIntMs = 0;
static volatile uint32_t s_intCount  = 0;
static void IRAM_ATTR touchIsr() {
  s_lastIntMs = millis();
  s_intCount++;
}

// One raw sample. Returns: 1 finger down, 0 no finger, -1 garbage frame.
static int readRaw(int& rx, int& ry) {
  // regs: 0x01 gesture, 0x02 finger count, 0x03/0x04 X, 0x05/0x06 Y
  uint8_t d[6];
  if (!readRegs(0x01, d, 6)) return -1;
  if (d[1] == 0) return 0;
  if (d[1] > 2) return -1;                // real values are 1-2; else noise

  rx = ((d[2] & 0x0F) << 8) | d[3];       // 0..239 (portrait)
  ry = ((d[4] & 0x0F) << 8) | d[5];       // 0..319
  if (rx > 239 || ry > 319) return -1;    // out-of-panel coords = noise
  return 1;
}

bool touch_read(TouchPoint& p) {
  static bool    stableDown = false;
  static uint8_t downCount = 0, upCount = 0;
  static TouchPoint last = {0, 0};

  int rx, ry;
  int s = readRaw(rx, ry);

  // INT gate: a finger-down report with no recent interrupt pulse means the
  // registers are stale or corrupted — treat it as "no finger". Only armed
  // once the INT line has proven itself alive.
  static bool gateArmed = false;
  if (!gateArmed && s_intCount > 0) {
    gateArmed = true;
    Serial.println("[touch] INT line alive - stale-report gating armed");
  }
  if (gateArmed && s == 1 && millis() - s_lastIntMs > TOUCH_INT_GATE_MS) s = 0;

  if (s == 1) {
    upCount = 0;

#if TOUCH_SWAP_XY
    int x = ry, y = rx;
#else
    int x = rx, y = ry;
#endif
#if TOUCH_INV_X
    x = SCREEN_W - 1 - x;
#endif
#if TOUCH_INV_Y
    y = SCREEN_H - 1 - y;
#endif
    x = constrain(x, 0, SCREEN_W - 1);
    y = constrain(y, 0, SCREEN_H - 1);

    // While accumulating toward a press, the point must hold still.
    // Noise frames pass the range checks but wander; restart the count.
    if (!stableDown && downCount > 0 &&
        (abs(x - last.x) > JITTER_PX || abs(y - last.y) > JITTER_PX)) {
      downCount = 1;
    } else if (downCount < 255) {
      downCount++;
    }
    last.x = x;
    last.y = y;

    if (downCount >= PRESS_FRAMES) stableDown = true;
  } else if (s == 0) {
    downCount = 0;
    if (upCount < 255) upCount++;
    if (upCount >= RELEASE_FRAMES) stableDown = false;
  }
  // s == -1: garbage frame — change nothing, hold current state

  if (stableDown) p = last;
  return stableDown;
}
