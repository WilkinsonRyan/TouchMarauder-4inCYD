# Flashing a blank 4″ CYD (no building required)

Prebuilt binaries for the **4″ ST7796 resistive** board (ESP32-3248S035R). Two
apps, one board: Marauder in `app0`, TouchBoard in `app1`. Flash all five files
once; it boots into Marauder (main menu → **TouchBoard** to switch).

## What you need
- **esptool** — `python3 -m pip install esptool` (needs Python 3).
- The board on USB, and its port name:
  - macOS: `ls /dev/cu.usbserial-*`
  - Linux: usually `/dev/ttyUSB0`
  - Windows: a `COMx` (see Device Manager)

## One command (replace `PORT`)

```sh
python3 -m esptool --chip esp32 --port PORT --baud 460800 \
  --before default_reset --after hard_reset write_flash -z \
  0x1000   firmware/bootloader.bin \
  0x8000   firmware/partitions.bin \
  0xe000   firmware/boot_app0.bin \
  0x10000  firmware/marauder_4in.bin \
  0x1f0000 firmware/touchboard_4in.bin
```

## Notes
- If it errors during transfer, drop the baud to `115200` (460800 can be flaky
  over a USB hub).
- **`marauder_4in.bin` is GPL-3.0** (matching source in [`../marauder-mod/`](../marauder-mod/));
  **`touchboard_4in.bin` is MIT** (source in [`../TouchBoard/`](../TouchBoard/)).
- This is the **4″ ST7796 resistive** board. It will not run on the 2.4″
  capacitive CYD — see [hord-brayden/TouchMarauder](https://github.com/hord-brayden/TouchMarauder)
  for that one.
```
