# Flashing a blank CYD

For the **ESP32-2432S024C** (2.4" capacitive CYD). Needs
[`esptool`](https://github.com/espressif/esptool) (`pip install esptool`).

## Flash map

| Offset     | File                                | What it is                         |
|------------|-------------------------------------|------------------------------------|
| `0x1000`   | `bootloader.bin`                    | ESP32 second-stage bootloader      |
| `0x8000`   | `partitions.bin`                    | dual-OTA table (two 1920KB slots)  |
| `0xe000`   | `boot_app0.bin`                     | OTA boot selector                  |
| `0x10000`  | `marauder_v1.1_touchboard-mod.bin`  | Marauder → **app0 / ota_0**        |
| `0x1f0000` | `touchboard.bin`                    | TouchBoard → **app1 / ota_1**      |

## Everything at once

Find your port (`ls /dev/cu.usbserial-*` on macOS, `/dev/ttyUSB*` on Linux, a
`COMx` on Windows), then:

```sh
python3 -m esptool --chip esp32 -p /dev/cu.usbserial-XXXX -b 460800 write_flash \
  0x1000   bootloader.bin \
  0x8000   partitions.bin \
  0xe000   boot_app0.bin \
  0x10000  marauder_v1.1_touchboard-mod.bin \
  0x1f0000 touchboard.bin
```

Boots into Marauder. Main menu → **TouchBoard** to switch; **BT** tab →
**Exit to Marauder** to switch back.

## Reflashing one app only

Keep the other app + the partition table intact — flash just its region, no
`--erase-all`:

```sh
# just Marauder
python3 -m esptool --chip esp32 -p PORT -b 460800 write_flash --flash_size keep 0x10000  marauder_v1.1_touchboard-mod.bin
# just TouchBoard
python3 -m esptool --chip esp32 -p PORT -b 460800 write_flash --flash_size keep 0x1f0000 touchboard.bin
```

## When it sulks

- **Won't enter download mode:** hold **BOOT**, tap **RESET**, release **BOOT**, retry.
- **Boot-loops after a switch:** stale Bluetooth keys in NVS. Wipe and it's fine:
  ```sh
  python3 -m esptool --chip esp32 -p PORT erase_region 0x9000 0x5000
  python3 -m esptool --chip esp32 -p PORT erase_region 0xe000 0x2000
  ```
- **Blank / garbled screen or dead touch:** you probably have the resistive
  `...024R` board, not the capacitive `...024C`. This build is capacitive-only.

`marauder_v1.1_touchboard-mod.bin` is GPL-3.0 (see `../marauder-mod/`).
