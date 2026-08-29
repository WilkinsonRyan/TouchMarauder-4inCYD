#pragma once
//
// Board: ESP32-3248S035R — 4" "Cheap Yellow Display", ST7796 320x480,
//        RESISTIVE (XPT2046) touch.  MCU: ESP32-WROOM-32 (no PSRAM).
//
// TouchBoard was originally written for the 2.4" capacitive board
// (ESP32-2432S024C, ILI9341 + CST820); this port moves it to the 4" resistive
// panel — the display/touch config lives in display.h.

// ---------- Bluetooth ----------
#define BLE_DEVICE_NAME   "TouchBoard"
#define BLE_MANUFACTURER  "BHord"

// How long a tapped key is held down before release, in ms. Must exceed one
// BLE connection interval or the host misses the press. 40ms is safe on
// iOS/Android/desktop; lower it for snappier fast typing if your host allows.
#define HID_TAP_HOLD_MS   40

// ---------- Display (ST7796, SPI2/HSPI) ----------
#define PIN_LCD_SCK   14
#define PIN_LCD_MOSI  13
#define PIN_LCD_MISO  12
#define PIN_LCD_CS    15
#define PIN_LCD_DC    2
#define PIN_LCD_RST   -1   // tied to EN
#define PIN_LCD_BL    27   // backlight, PWM

#define SCREEN_W      320  // ST7796 portrait, the T9 orientation (was 240)
#define SCREEN_H      480  // (was 320)

// ---------- Resistive touch (XPT2046, shares the display HSPI bus) ----------
// Ported from the 2.4" capacitive CST820. Only the chip-select is needed; the
// XPT2046 is configured in display.h and read via LovyanGFX tft.getTouch().
#define PIN_TOUCH_CS  33   // T_CS on this board (was the CST820 I2C SDA pin)

// ---------- T9 typing (traditional buffered multi-tap) ----------
#define T9_MULTITAP_MS  1100  // window to tap the same key again and cycle
#define T9_LONGPRESS_MS 600   // hold a key this long to type its digit
#define T9_MIN_TAP_MS   35    // shorter contacts are ghost blips, ignored

// ---------- Onboard RGB LED (active LOW) ----------
#define PIN_LED_R     4
#define PIN_LED_G     16
#define PIN_LED_B     17

// ---------- microSD card (shared VSPI bus: SCK 18 / MISO 19 / MOSI 23) ----------
// The SD slot is on the ESP32's default (VSPI) SPI bus, separate from the
// display (which LovyanGFX drives on HSPI 12/13/14), so the two don't collide.
// Only the chip-select differs per board; on this 4" CYD it's GPIO 5.
#define PIN_SD_CS  5

// ---------- Battery gauge (18650 on the CYD's onboard battery JST) ----------
// The battery connector feeds this pin through an on-board resistor divider.
// GPIO 34 is ADC1 + input-only, so it reads correctly even while Marauder's
// WiFi is up (ADC2 pins go dead once WiFi starts) — this is the same pin the
// Marauder half already uses for its own battery readout on this board.
//
// VERIFY before trusting the % on screen: define VBAT_DEBUG (below), open the
// serial monitor, and compare the printed "cell=####mV" to a multimeter across
// the 18650. If it reads ~half the real voltage, your board's divider is not
// 1:2 — set VBAT_DIVIDER to the real ratio. If it's uniformly a few % off, nudge
// VBAT_TRIM. If the pin is wrong for your board (reads ~0 or garbage), change
// PIN_VBAT. If the Marauder half already shows a correct %, this pin is right.
#define PIN_VBAT       34
#define VBAT_DIVIDER   2.0f    // on-board divider ratio (cell V = pin V * this)
#define VBAT_TRIM      1.0f    // fine calibration multiplier; leave 1.0 unless off
//#define VBAT_DEBUG           // uncomment to print raw mV / % to serial for calibration
