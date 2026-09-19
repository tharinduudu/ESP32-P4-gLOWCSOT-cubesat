# ESP32-S3-GEEK BLE Live Display

This is the quick-look screen firmware for the Waveshare ESP32-S3-GEEK. It does not join the detector Wi-Fi network. Instead, it passively scans for compact BLE advertisements from the ESP32-P4 detector and shows the latest live count snapshot on the onboard ST7789 display.

The goal is to keep the detector Wi-Fi off during data taking, because Wi-Fi activity has been seen to add noise. BLE advertisements are short, connectionless packets, so the detector can publish a live status display with much less radio activity than the web server.

## What It Shows

The display highlights:

- `01`, `02`, `12`, and `012` coincidence counts.
- Raw `G6`, `G5`, and `G16` channels.
- HV byte.
- Counting or wait state.
- SD card ready state.
- FPGA OK state.
- Time-sync state.
- BLE RSSI and stale-packet warning.

The count values are a live snapshot of the P4 counters. The SD card on the P4 remains the long-term data record.

## BLE Packet

The P4 sends a non-connectable BLE advertisement with manufacturer data:

| Field | Size | Notes |
| --- | ---: | --- |
| Company ID | 2 bytes | `0xffff` temporary/local ID |
| Magic | 2 bytes | `MU` |
| Version | 1 byte | `1` |
| Sequence | 1 byte | increments each packet |
| Epoch | 4 bytes | Unix time from the P4 |
| Counts | 14 bytes | seven unsigned 16-bit counters |
| Status | 1 byte | time, counting, FPGA, SD flags |
| HV | 1 byte | current MAX1932 byte |

The packet is intentionally small enough to fit inside legacy BLE advertising data.

## Build And Flash

From this directory:

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem11301 flash monitor
```

If the port changes, check:

```sh
ls /dev/cu.usb*
```

## Power Notes

The firmware uses passive BLE scanning and a conservative CPU profile. ESP-IDF currently prevents automatic light sleep while Bluetooth is enabled on this target, so the LCD backlight and BLE scan duty cycle dominate power use.

For longer screen runtime, reduce the LCD backlight in hardware or add PWM backlight dimming in `main/main.c`.
