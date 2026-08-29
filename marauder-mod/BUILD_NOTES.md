# The Marauder half (4″ ST7796)

This is **not** a copy of ESP32 Marauder. It's a set of edits on top of
[Fr4nkFletcher's CYD fork](https://github.com/Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display)
**v1.4.3** (itself a fork of [justcallmekoko's Marauder](https://github.com/justcallmekoko/ESP32Marauder)).
Everything here is **GPL-3.0** (see [`LICENSE`](LICENSE)) because Marauder is.

The changed files live in [`esp32_marauder/`](esp32_marauder/); the added display
fonts live in [`fonts/`](fonts/). Drop them onto a clean Fr4nkFletcher v1.4.3
checkout (see "Building it" below).

## What changed vs stock v1.4.3

- **Board target:** `CYD_35` — selected via `MARAUDER_V4` in `configs.h` — the
  4″ ST7796 320×480 **resistive** (XPT2046) panel.
- **A theme engine** (`ui_theme`: Light / Dark / Hacker) with a
  **Toggle Theme** submenu, persisted to NVS. `itemColor()` / `themeOutline()`
  drive per-theme menu colors (Hacker = neon green), and `drawThemeGlyph()`
  draws procedural Hacker icons.
- **Hacker "Matrix" rain** behind the menus (`stepHackerRain()` /
  `restampHackerMenu()`), plus a **splash** on theme-select and boot
  (`matrixSplash()`).
- **A dimmed idle screensaver** (`runScreensaver()`) after ~25s no-touch — themed
  animation + cycling word-wrapped quotes, 40% brightness, tap to wake.
- **Brightness control** — LEDC PWM backlight on `TFT_BL` (GPIO 27), `±10%`,
  in Settings. `applyBrightness()` / display prefs in `esp32_marauder.ino`.
- **On-screen touch calibration** ("Calibrate Touch"), stored in NVS.
- **UI overhaul** — JetBrains Mono menu font, full-width tappable rows with
  separators, marquee for over-long labels, on-screen ▲▼ scroll arrows.
- **AP Mimic menu launcher** — `WIFI_ATTACK_MIMIC` was handled in `WiFiScan.cpp`
  but had no menu button; added one under WiFi → Attacks.
- **New main-menu tile: `TouchBoard`** — sets the boot partition to `ota_1` and
  reboots into the keyboard.
- **GPS off** — no GPS module on this board; GPS init hung the boot. `HAS_GPS`
  stays undefined (effectively the `nogps` build).
- **NimBLE bond namespace** kept separate so Marauder and TouchBoard don't
  clobber each other's Bluetooth keys in NVS.

## Building it

Marauder v1.4.3 was written against the **old ESP32 core (2.0.x)** — it uses WiFi
internals that core 3.x removed. So:

- **esp32 core `2.0.17`** (not 3.x). Install it isolated if you don't want to
  disturb your main setup:
  `ARDUINO_DIRECTORIES_DATA=/some/scratch arduino-cli core install esp32:esp32@2.0.17`
- **TFT_eSPI:** use the **`3.5R`** variant from the upstream fork's
  `libraries/TFT_eSPI-CYD/3.5R/` as your `TFT_eSPI` (the fork ships a dozen
  board-specific copies — the compiler will grab the wrong one otherwise). Then:
  - add the four `fonts/JetBrainsMono*pt7b.h` here into TFT_eSPI's
    `Fonts/GFXFF/` and `#include` them from `gfxfont.h`;
  - in `User_Setup.h`, **comment out `TFT_BL` / `TFT_BACKLIGHT_ON`** so TFT_eSPI
    stops driving the backlight pin and the LEDC PWM brightness control works.
- **Partition scheme:** `min_spiffs` (this is the dual-app layout — Marauder in
  `app0`, TouchBoard in `app1`).
- **One extra link flag:**
  `compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition` (Marauder defines
  globals in headers; identical across translation units, so "keep the first" is
  safe).

Roughly:

```sh
git clone https://github.com/Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display
cd ESP32-Marauder-Cheap-Yellow-Display
# copy this folder's esp32_marauder/*.{cpp,h,ino} over esp32_marauder/,
# add fonts/*.h into the 3.5R TFT_eSPI GFXFF, comment TFT_BL in User_Setup.h
arduino-cli compile \
  --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs,FlashSize=4M,PSRAM=disabled,CPUFreq=240,FlashFreq=80,FlashMode=dio" \
  --libraries ./build_libs \
  --build-property "compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition" \
  esp32_marauder
```

## Flashing just this half

Flash **only** the app0 region so TouchBoard in app1 survives:

```sh
python3 -m esptool --chip esp32 -p /dev/cu.usbserial-XXXX -b 460800 \
  write_flash --flash_size keep 0x10000 esp32_marauder/build/esp32_marauder.ino.bin
```

(Clearing otadata — `erase_region 0xe000 0x2000` — makes it boot straight into
Marauder afterward.)

## If it misbehaves

- **Boot-loop / stack smash after touch init** → NVS bond collision. Wipe:
  `esptool erase_region 0x9000 0x5000` then `erase_region 0xe000 0x2000`.
- **Hangs after the battery check** → GPS is on. Keep `HAS_GPS` undefined.
- **Wrong colors / garbage screen** → wrong TFT_eSPI variant. Use `3.5R`.
- **Backlight brightness does nothing** → `TFT_BL` still defined in
  `User_Setup.h`; comment it out.
