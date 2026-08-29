# TouchMarauder — 4″ ST7796 CYD

Two completely different firmwares living on one $15 board, and you flip between
them with a tap. No recompiling, no reflashing, no picking a lane.

This is a **4-inch port** of [TouchMarauder](https://github.com/hord-brayden/TouchMarauder)
(originally built by [hord-brayden](https://github.com/hord-brayden) for the 2.4″
capacitive board), rebuilt for the **4″ ST7796 resistive "Cheap Yellow Display"**
(ESP32-3248S035R) — and given a full custom interface on top.

One board runs:

- **Marauder** — [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder),
  the WiFi/Bluetooth investigation toolkit: AP/station scans, packet & handshake
  capture, deauth, beacon spam, Evil Portal, BLE analyzer and spam suite.
- **TouchBoard** — a full touchscreen Bluetooth keyboard (T9, numpad, arrows,
  landscape QWERTY) that pairs with any phone/laptop/TV as a real HID keyboard.

Both live in the ESP32's two OTA app slots. Marauder gets a **TouchBoard** tile
in its main menu; TouchBoard gets an **Exit to Marauder** button on its BT tab.
Tap one, the board reboots into the other in about a second — nothing is copied
or erased, both firmwares just sit resident, waiting their turn.

## What this build adds on top of TouchMarauder

The dual-app trick and the TouchBoard app are hord-brayden's. This fork:

- **Ports the whole thing to the 4″ ST7796 resistive CYD** (`CYD_35`) — a
  different display and a different (XPT2046) touch controller than the 2.4″
  original. TouchBoard's LovyanGFX panel + touch config were rebuilt for it.
- **A theme engine** — Light / Dark / **Hacker** / **USA Patriot**, chosen from
  a "Toggle Theme" picker and remembered across reboots, in *both* apps.
  - **Hacker:** black + neon-green with grey outlines, custom procedural glyphs,
    and live "Matrix" digital rain flowing behind the menus.
  - **USA Patriot:** navy background with red/white/blue menus (flag-striped
    keys on the keyboard), star / flag / shield glyphs, and a star-spangled
    screensaver.
- **A dimmed idle screensaver** (in both apps) — after ~25s untouched the screen
  drops to 40% and plays a themed animation (Matrix rain, or a red/white/blue
  star rain in USA Patriot) with large, random, word-wrapped cycling quotes;
  tap to wake.
- **Brightness control** (PWM, ±10%) and **on-screen touch recalibration**.
- **A JetBrains Mono UI** — full-width tappable menu rows with separators,
  horizontal marquee for long labels, on-screen scroll arrows, snappier menus.
- **Exposes the AP Mimic attack** (the handler was already in Marauder; this adds
  the missing menu launcher).

## How the switch works

The CYD's 4 MB flash is carved into two app slots (`ota_0`, `ota_1`):

```
  app0 / ota_0  ->  Marauder
  app1 / ota_1  ->  TouchBoard
```

Each app calls `esp_ota_set_boot_partition()` on the other slot and reboots.
Both use NimBLE and were fighting over the same NVS bond namespace (a boot-loop);
Marauder's bond store lives under its own namespace so they leave each other's
Bluetooth keys alone.

## Hardware

- **ESP32-3248S035R** — 4″ CYD, **ST7796** 320×480 display, **resistive** XPT2046
  touch, ESP32-WROOM-32, no PSRAM.
- microSD slot on the back (FAT32) — used for packet captures, Evil Portal HTML,
  and saved scan lists.
- USB-C for power + flashing.

> The 2.4″ capacitive original lives at
> [hord-brayden/TouchMarauder](https://github.com/hord-brayden/TouchMarauder).
> This fork targets the 4″ resistive board and will **not** run on that one
> unmodified (different display + touch driver).

## Flash a blank board (no building)

Prebuilt binaries are in [`firmware/`](firmware/) — one esptool command flashes
both apps onto a blank 4″ CYD. See [`firmware/FLASH.md`](firmware/FLASH.md).
It boots into Marauder; main menu → **TouchBoard** to cross over.

## Build from source

- **TouchBoard** → [`TouchBoard/`](TouchBoard/) — Arduino, esp32 core 3.x,
  LovyanGFX 1.2.7 + NimBLE 2.5.0. See its [README](TouchBoard/README.md).
- **Marauder mod** → [`marauder-mod/`](marauder-mod/) — a handful of changed
  files on top of Fr4nkFletcher's CYD fork (not a whole copy). Recipe and the
  exact changes are in [`marauder-mod/BUILD_NOTES.md`](marauder-mod/BUILD_NOTES.md).

Both use the standard dual layout — Marauder app at `0x10000`, TouchBoard app at
`0x1f0000` (`min_spiffs` partition scheme).

## Docs

A plain-English guide to every Marauder menu item — what it does, what shows on
screen, and what to do with what you capture — is in
[`docs/Marauder-Field-Manual.md`](docs/Marauder-Field-Manual.md).

## Credits

- **[justcallmekoko](https://github.com/justcallmekoko/ESP32Marauder)** — the
  original ESP32 Marauder. None of the Marauder half exists without them.
- **[Fr4nkFletcher](https://github.com/Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display)**
  — the Cheap Yellow Display port this is based on.
- **[hord-brayden](https://github.com/hord-brayden/TouchMarauder)** — TouchBoard
  (the from-scratch touchscreen BLE keyboard) and the original TouchMarauder
  dual-app mod this build forks and extends.
- **[WilkinsonRyan](https://github.com/WilkinsonRyan)** — the 4″ ST7796 (`CYD_35`)
  port of the whole thing, and the interface layer added on top of both apps:
  the Light / Dark / Hacker / USA Patriot theme engine, the Matrix + star-spangled screensavers,
  brightness control, touch recalibration, the JetBrains Mono UI,
  and exposing the AP Mimic attack.

## Licensing

- **TouchBoard** and this repo's own glue: **MIT** (see [`LICENSE`](LICENSE)).
- The **Marauder mod is GPL-3.0**, because Marauder is — see
  [`marauder-mod/LICENSE`](marauder-mod/LICENSE) and the credits above.

## One serious note

Marauder ships real transmit tools — deauth, beacon spam, Evil Portal, BLE spam.
Great for learning and for testing **your own** gear and networks. Pointing them
at networks or devices you don't own is illegal basically everywhere, so don't.
Be the person who knows how WiFi works, not the person explaining it to a judge.
