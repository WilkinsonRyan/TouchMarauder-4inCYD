//
// Battery gauge — see battery.h. Reads the CYD's on-board battery divider on
// PIN_VBAT (ADC1), converts to cell voltage, and maps that to a percent along a
// Li-ion discharge curve. All the pin/divider/trim knobs live in config.h so
// this file never needs editing to match a different board or a mismeasured
// divider.
//
#include <Arduino.h>
#include "battery.h"
#include "config.h"

// ---- sampling ----
// A BLE keyboard + (on the Marauder side) a WiFi radio yank the rail around, so
// a single analogRead is noisy. Take a burst, drop the extremes via a median,
// then feed that through an exponential moving average for on-screen calm.
static const int      SAMPLES     = 11;     // odd -> a clean median
static const float    EMA_ALPHA   = 0.20f;  // 0..1; lower = smoother/slower
static const uint16_t PRESENT_MV  = 2000;   // below this (per cell) => "no cell"

static float    ema_mv   = 0.0f;   // smoothed CELL millivolts
static bool     seeded   = false;
static uint8_t  last_pct = 0;

// ---- charge detection (no charge-status pin on this board) ----
// The TP4054 charger holds the cell at its float voltage (~4.2V) while on USB.
// Under this board's constant ~150mA+ load an UNplugged cell can't sit that
// high, so a sustained reading at/above CHG_HOLD_MV means "on charger". We also
// flag charging when the voltage is clearly climbing (the CC phase, before it
// reaches float). Best-effort: with only a voltage divider there's no way to
// know "charge complete" vs "topped off and still plugged".
static const float    CHG_HOLD_MV = 4200.0f;  // held here under load => on charger
static const float    CHG_RISE_MV = 4.0f;     // rise between updates => charging
static float    prev_ema = 0.0f;
static bool     charging = false;

// Li-ion (single cell) resting/light-load discharge curve: {cell mV, percent}.
// Voltages between points are linearly interpolated. Deliberately conservative
// at the top (a "full" cell sits ~4.15-4.20 under no load) and steep at the
// bottom where Li-ion voltage falls off a cliff.
struct VPoint { uint16_t mv; uint8_t pct; };
static const VPoint CURVE[] = {
  {4200, 100}, {4150,  95}, {4110,  90}, {4080,  85}, {4020,  80},
  {3980,  75}, {3950,  70}, {3910,  65}, {3870,  60}, {3850,  55},
  {3840,  50}, {3820,  45}, {3800,  40}, {3790,  35}, {3770,  30},
  {3750,  25}, {3730,  20}, {3710,  15}, {3690,  10}, {3610,   5},
  {3270,   0},
};
static const int CURVE_N = sizeof(CURVE) / sizeof(CURVE[0]);

static uint8_t mvToPct(uint16_t mv) {
  if (mv >= CURVE[0].mv)            return 100;
  if (mv <= CURVE[CURVE_N - 1].mv) return 0;
  for (int i = 0; i < CURVE_N - 1; i++) {
    const VPoint& hi = CURVE[i];
    const VPoint& lo = CURVE[i + 1];
    if (mv <= hi.mv && mv >= lo.mv) {
      float f = (float)(mv - lo.mv) / (float)(hi.mv - lo.mv);   // 0 at lo, 1 at hi
      return (uint8_t)(lo.pct + f * (hi.pct - lo.pct) + 0.5f);
    }
  }
  return 0;
}

// One burst-sampled, median-filtered CELL voltage in mV (post-divider math).
// analogReadMilliVolts() applies the ESP32's factory ADC calibration, so this
// is already in real millivolts at the pin before we undo the divider.
static uint16_t sampleCellMv() {
  uint16_t buf[SAMPLES];
  for (int i = 0; i < SAMPLES; i++) buf[i] = analogReadMilliVolts(PIN_VBAT);
  // insertion sort (SAMPLES is tiny) -> median
  for (int i = 1; i < SAMPLES; i++) {
    uint16_t v = buf[i];
    int j = i - 1;
    while (j >= 0 && buf[j] > v) { buf[j + 1] = buf[j]; j--; }
    buf[j + 1] = v;
  }
  uint16_t node_mv = buf[SAMPLES / 2];
  return (uint16_t)(node_mv * VBAT_DIVIDER * VBAT_TRIM + 0.5f);
}

void battery_begin() {
  analogReadResolution(12);
  // 11 dB attenuation -> full-scale ~2.5-3.1V at the pin, covering the ~2.1V a
  // full cell shows through the ÷2 divider.
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  uint16_t mv = sampleCellMv();
  ema_mv   = mv;
  prev_ema = mv;
  seeded   = (mv > PRESENT_MV);
  last_pct = mvToPct(mv);
  charging = (ema_mv >= CHG_HOLD_MV);
#ifdef VBAT_DEBUG
  Serial.printf("[batt] init cell=%umV pct=%u (pin %d, /%.2f, trim %.3f)\n",
                mv, last_pct, PIN_VBAT, (double)VBAT_DIVIDER, (double)VBAT_TRIM);
#endif
}

// Sample once and fold into the EMA. Cheap, but still an 11-read burst, so the
// caller (ui_tick) drives this on a slow cadence (~15s). The getters below just
// return the cached result, so redraws are free and the glyph never jitters
// from a mid-frame re-sample.
void battery_update() {
  uint16_t mv = sampleCellMv();
  if (!seeded) { ema_mv = mv; prev_ema = mv; seeded = (mv > PRESENT_MV); }
  else         { ema_mv += EMA_ALPHA * (mv - ema_mv); }
  last_pct = mvToPct((uint16_t)(ema_mv + 0.5f));

  // Charge state: rising => charging (CC phase); held at float => on charger;
  // clearly falling or sitting low => running on battery. Flat-but-below-float
  // keeps whatever it was only if still high, else off.
  float delta = ema_mv - prev_ema;
  if      (delta >=  CHG_RISE_MV) charging = true;
  else if (delta <= -CHG_RISE_MV) charging = false;
  else                            charging = (ema_mv >= CHG_HOLD_MV);
  prev_ema = ema_mv;
#ifdef VBAT_DEBUG
  Serial.printf("[batt] cell=%umV ema=%umV pct=%u chg=%d\n",
                mv, (uint16_t)(ema_mv + 0.5f), last_pct, charging ? 1 : 0);
#endif
}

uint16_t battery_millivolts() { return (uint16_t)(ema_mv + 0.5f); }
uint8_t  battery_percent()    { return last_pct; }
bool     battery_present()    { return seeded && ema_mv > PRESENT_MV; }
bool     battery_charging()   { return seeded && charging; }
