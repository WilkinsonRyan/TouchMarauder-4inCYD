#pragma once
#include <stdint.h>
//
// Battery gauge for the 18650 (single Li-ion cell) on the CYD's onboard battery
// JST. On the ESP32-3248S035R that connector feeds an analog pin through an
// on-board 1:2 resistor divider, so a fully charged 4.2V cell reads ~2.1V at
// the pin — within the ADC's range. The pin (PIN_VBAT) is on ADC1, so it keeps
// reading correctly even while Marauder has WiFi up (ADC2 does not).
//
// Nothing here transmits; it only samples the divider and maps volts -> percent
// against a Li-ion discharge curve, smoothed so the on-screen glyph doesn't
// twitch under the load spikes of a BLE/WiFi radio.

void     battery_begin();          // configure the ADC pin; call once from ui_begin()
void     battery_update();         // sample once and fold into the average; call on a slow cadence
uint16_t battery_millivolts();     // last smoothed CELL voltage in mV (post-divider), cached
uint8_t  battery_percent();        // last 0..100 reading, cached (mapped to the Li-ion curve)
bool     battery_present();        // false if the pin reads too low to be a real cell
