# Build, Flash, And Development Guide

## Toolchain

Use ESP-IDF v5.5.x with ESP32-P4 support.

Typical setup:

```sh
. /path/to/esp-idf/export.sh
```

Then build from the repository root:

```sh
idf.py set-target esp32p4
idf.py build
```

Flash:

```sh
idf.py -p /dev/cu.usbmodem5B5E1289611 flash monitor
```

If the serial port differs:

```sh
ls /dev/cu.usb*
```

## Firmware Layout

The firmware is split into detector sections instead of keeping every subsystem in one large file:

| File | Responsibility |
| --- | --- |
| `main/main.c` | Boot sequence and task startup |
| `main/app_common.h` | Shared constants, detector types, and cross-module declarations |
| `main/app_state.c` | Shared runtime state for counts, HV, SD, BME280, Wi-Fi, and run metadata |
| `main/hardware.c` | GPIO rail, SPI, FPGA programming, DAC writes, HV control, and I2C bus setup |
| `main/counters.c` | GPIO interrupt counters, minute records, ring buffer, and serial count output |
| `main/storage.c` | SD card mount, run filenames, CSV formatting, and count/environment file writes |
| `main/environment.c` | BME280 forced reads, 5-minute averages, and temperature compensation |
| `main/web.c` | Embedded web UI, HTTP API, Wi-Fi AP, and power-saving web controls |
| `main/console.c` | USB serial maintenance commands |
| `main/ble_broadcast.c` | BLE live-count advertisements for the S3 quick-look display |

Important constants live in `main/app_common.h`:

| Constant | Purpose |
| --- | --- |
| `READOUT_PROFILE_OCT2025` | selects Oct-2025 readout profile |
| `STARTUP_HV_BYTE` | MAX1932 HV startup byte |
| `STARTUP_DAC_CODE` | DAC channels 0-3 startup code |
| `STARTUP_DAC_THRESHOLD_CODE` | DAC channels 4-7 threshold startup code |
| `HV_SETTLE_MS` | delay after HV changes |
| `COUNTER_PERIOD_MS` | one-minute count period |
| `BME280_AVG_PERIOD_MS` | environment average period |

## Embedded FPGA Bitstream

The FPGA bitstream is embedded by CMake:

```text
main/fpga.bin
```

To update it, replace `main/fpga.bin` with the desired iCE40 `.bin`, then rebuild and flash.

The current file was copied from the Oct-2025 readout:

```text
top_50MHz_led100_dt200_pi10us.bin
```

## Startup Sequence In Code

The main boot path is in `app_main()`:

1. NVS init.
2. GPIO rail setup.
3. FPGA control GPIO setup.
4. SPI setup.
5. HV off.
6. Counter ISR setup.
7. FPGA programming.
8. DAC startup values.
9. HV enable and settle.
10. SD card init.
11. BME280 init.
12. Counter task.
13. Console task.
14. BLE live-count broadcaster.
15. Wi-Fi and HTTP server.
16. Power management.

## Web UI

The web interface is embedded as a C string in `main/web.c`. The main API endpoints are:

| Endpoint | Purpose |
| --- | --- |
| `/api/status` | JSON status, live counts, records |
| `/api/latest.txt` | newest count row for small displays |
| `/api/log.csv` | RAM log download |
| `/api/sd.csv` | active SD muon file download |
| `/api/env.csv` | active SD environment file download |
| `/api/sd_files` | list previous SD files |
| `/api/sd_file?name=...` | download one previous file |
| `/api/time?epoch=...` | set clock |
| `/api/run_label?label=...` | set run label |
| `/api/hv` | set HV or turn it off |
| `/api/dac` | set one DAC channel |
| `/api/fpga` | safe FPGA reflash |
| `/api/power_save` | shut Wi-Fi off |
| `/api/wifi_keep_on` | disable auto-off |

## BLE Live Display

The P4 firmware broadcasts live detector state through non-connectable BLE advertisements. The ESP32-S3-GEEK display firmware lives in:

```text
display/s3-geek-ble-display
```

Build and flash it separately:

```sh
cd display/s3-geek-ble-display
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem11301 flash monitor
```

The display does not connect to Wi-Fi. It passively scans for the P4 manufacturer-data packet and redraws the latest coincidence counts, raw counts, HV state, SD state, FPGA state, time-sync state, and RSSI.

## Power And Noise Notes

The firmware is optimized for field operation:

- Wi-Fi can be disabled after setup.
- Wi-Fi auto-off runs when no client is connected.
- The S3 quick-look display uses BLE advertisements instead of the Wi-Fi web API.
- HV is cycled safely during Wi-Fi shutdown.
- SD logging continues without Wi-Fi.
- Serial minute count output is suppressed after power-saving mode starts.
- PSRAM is disabled in the default build.
- CPU power management can scale idle CPU down after Wi-Fi is off.

## Commit Checklist

Before pushing changes:

```sh
idf.py build
git status --short
```

If hardware is connected:

```sh
idf.py -p /dev/cu.usbmodem5B5E1289611 flash monitor
```

Healthy boot should show SD card info and `startup HV enable result: ESP_OK`.
