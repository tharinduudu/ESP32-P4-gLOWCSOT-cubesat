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

## Main Firmware File

Most logic is in:

```text
main/main.c
```

Important constants near the top:

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
14. Wi-Fi and HTTP server.
15. Power management.

## Web UI

The web interface is embedded as a C string in `main/main.c`. The main API endpoints are:

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

## Power And Noise Notes

The firmware is optimized for field operation:

- Wi-Fi can be disabled after setup.
- Wi-Fi auto-off runs when no client is connected.
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
