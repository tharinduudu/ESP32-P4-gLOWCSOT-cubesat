# ESP32-P4 gLOWCOST CubeSat Muon Readout

ESP-IDF firmware for running a gLOWCOST/MPPC cosmic-muon detector readout on a Waveshare ESP32-P4 Module DEV KIT. It adapts the Raspberry Pi HAT readout workflow to the ESP32-P4 40-pin header, including FPGA programming, high-voltage control, DAC threshold setup, one-minute count logging, BME280 environment logging, Wi-Fi and USB-UART monitoring, and an optional ESP32-S3-GEEK BLE live display.

## Introduction

This project is part of the gLOWCOST detector development effort connected to the [Georgia State University global cosmic-ray muon detector network](https://cosmic.gsu.edu/). The GSU project uses low-cost scintillator and SiPM-based muon detectors to build a distributed network for monitoring cosmic-ray count changes related to space weather and atmospheric conditions.

This repository contains the ESP32-P4 embedded readout used for portable field and flight measurements. The firmware starts the detector from a USB power bank, programs the FPGA, sets the high-voltage and DAC operating points, and records timestamped count data to a microSD card. The detector can be checked over Wi-Fi or through a Mac connected to the board's USB-UART port. After setup, Wi-Fi can be turned off so the detector can continue a quieter flight run.

## Hardware Photos

| Closed case | Open case |
| --- | --- |
| ![gLOWCOST detector closed inside its case](docs/assets/detector-closed-case.jpg) | ![gLOWCOST detector opened inside its case](docs/assets/detector-open-case.jpg) |
| Detector powered from USB power bank | Readout stack |
| ![gLOWCOST detector powered from a USB power bank](docs/assets/detector-usb-power-bank.jpg) | ![ESP32-P4 and detector readout stack](docs/assets/detector-readout-stack.jpg) |
| Readout board | Connector view |
| ![Detector readout board with SiPM connectors](docs/assets/detector-readout-board.jpg) | ![Detector connector side with coax cables](docs/assets/detector-connectors.jpg) |
| Side view | 5000 mAh power bank |
| ![Detector side view with cables connected](docs/assets/detector-side-view.jpg) | ![USB power bank used for field runs](docs/assets/power-bank.jpg) |

## Quick Start: Using The Detector

This is the normal field-use procedure for an already-built detector.

### 1. Power Up

1. Insert a FAT32-formatted microSD card before power-up.
2. Power the ESP32-P4 from a USB power bank.
3. Leave the detector still for startup.
4. Wait for the firmware to flash the FPGA, set the DAC/HV values, and finish the 10-second HV settle delay.
5. A new muon data file and environment file are created on the SD card for each detector start.

At boot the firmware turns HV off first, programs the FPGA, sets the DACs, turns HV on, waits 10 seconds, then starts recording. This avoids logging startup noise as detector data.

### 2. Open The Web Page

After power-up, connect your phone or computer to:

| Field | Value |
| --- | --- |
| Wi-Fi SSID | `MuonReadout` |
| Wi-Fi password | `glowcost` |
| Web page | `http://192.168.4.1` |

The browser automatically syncs time to the detector. The first browser sync in each boot renames the active SD files using the browser's local clock time.

For a wired flight monitor without Wi-Fi, first download and unzip the
[packaged Mac monitor](https://github.com/tharinduudu/ESP32-P4-gLOWCSOT-cubesat/raw/refs/heads/main/dist/Muon-UART-Monitor-macOS.zip).
It contains `uart_web_monitor.py`, `Start Muon UART Monitor.command`, and a Read
Me. Keep the detector powered from its normal USB-C power input, connect the
other USB-UART Type-C port to a Mac. Open Terminal, type `python3` and a space,
drag the extracted `uart_web_monitor.py` into Terminal, and press Return. This
avoids needing to open the `.command` launcher. Python 3 must be installed.

From a repository checkout, run:

```sh
python3 tools/uart_web_monitor.py --open-browser
```

Then open `http://127.0.0.1:8080`. This local page provides live counts, status, time sync, run labels, controls, and SD file downloads through UART. Unplugging the Mac does not reset the minute counters or stop SD logging; the power bank continues to run the detector.

### 3. Check The Run

1. Confirm the web page shows **SD Card Ready**.
2. Confirm FPGA status is good and HV is on.
3. Check the highlighted live one-minute coincidence counts.
4. Add a run label if useful, such as location, flight number, or test name.
5. Download a quick file only if needed.

### 4. Run Quietly On Battery

Wi-Fi can add noise, so use it only for setup and quick checks.

1. After the SD card, time, label, and counts look good, press **Turn Wi-Fi Off** or let Wi-Fi auto-off.
2. The firmware briefly turns HV off during Wi-Fi shutdown, then turns HV back on and waits for the normal settle delay.
3. The detector keeps counting and logging to the SD card while Wi-Fi is off. The local USB-UART page remains available on a connected Mac.
4. Leave the power bank connected for the full run.

Use the USB-UART monitor while Wi-Fi is off, or power-cycle the detector to restore the Wi-Fi setup page. A new run file will be created on the next start.

### 5. Download Data

For web downloads, connect to `MuonReadout` while Wi-Fi is active or open the local USB-UART page on the Mac, then use:

- **Download Current File** for the active muon count file
- **Download Environment File** for the active BME280 file
- **Previous SD Files** for older `.csv` and `.log` files, newest first

For the separate native USB maintenance console:

```sh
python3 -m serial.tools.miniterm /dev/cu.usbmodem1101 115200
```

Useful serial commands:

```text
status
counts
sd status
sd tail current 20
sd list
```

## Updating Firmware

Normal detector use does not require reflashing. Use this only when installing a new firmware version.

Before flashing, fully power the detector off and let the HV discharge. For the safest download-mode entry, disconnect the power bank and Mac, hold **BOOT**, connect only the USB-UART cable to the Mac, then release **BOOT** after the port appears. This prevents the old application from starting and enabling HV before the flash.

```sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
```

If your Mac gives the board a different serial port, check with:

```sh
ls /dev/cu.usbmodem*
```

## Important Safety Behavior

This detector controls high voltage. The firmware turns HV off before startup transitions, FPGA flashing, and Wi-Fi shutdown/restart operations. Counting is disabled while HV is off or settling.

Do not connect or disconnect scintillator/SiPM hardware while HV is enabled. Power down or turn HV off first.

## Current Operating Settings

| Setting | Current value |
| --- | --- |
| FPGA bitstream | `top_50MHz_3layer_ch123_led100_dead200_pi10us.bin` embedded as `main/fpga.bin` |
| HV byte | `0xea` |
| HV settle delay | 10 seconds |
| DACx578 address | `0x47` |
| SiPM DAC channels 0-3 | `0x2f1` |
| Threshold DAC channels 4-7 | `0x080` |
| Count interval | 60 seconds |
| Environment interval | 5-minute averages |
| Wi-Fi SSID | `MuonReadout` |
| Wi-Fi password | `glowcost` |
| Wi-Fi auto-off | 5 minutes after startup, unless **Keep Wi-Fi On** is selected |
| USB-UART monitor | 115200 baud on GPIO37/GPIO38; local dashboard at `http://127.0.0.1:8080` |
| BLE display name | `MuonP4` |

## Web Interface

The web page highlights SD-card readiness, live one-minute coincident counts, HV/FPGA/DAC controls, environment readings, current log downloads, and previous SD files.

![ESP32-P4 muon readout web interface](docs/assets/webserver-screenshot.png)

The active profile is adapted from `tharinduudu/mppcInterface-Oct-2025` for the new layout 3v0 readout. The current Pi reference snapshot checked during ESP32 sync was commit `9c9868d`.

## Current Hardware Profile

- Controller: Waveshare ESP32-P4 Module DEV KIT, tested on ESP32-P4 rev v1.3.
- Readout board: gLOWCOST/MPPC Raspberry Pi HAT-style detector interface.
- FPGA: iCE40 programmed from embedded `main/fpga.bin`.
- Active FPGA bitstream: `top_50MHz_3layer_ch123_led100_dead200_pi10us.bin`, using physical detector channels CH1/CH2/CH3.
- Logged counter order: CH1-CH2, CH1-CH3, CH2-CH3, CH1, CH2, CH3. The spare CH0 output on BCM25 is not logged.
- High voltage: MAX1932 controlled over SPI, startup byte `0xea`.
- DAC: DACx578 on I2C address `0x47`.
- Startup DAC values: channels 0-3 at `0x2f1`; threshold channels 4-7 at `0x080`.
- Bias target: restored to the earlier bench-tested voltage profile while threshold tuning continues.
- Environment sensor: optional BME280 on I2C `0x76` or `0x77`.
- Storage: onboard microSD card in SPI mode.
- Optional quick-look display: Waveshare ESP32-S3-GEEK running `display/s3-geek-ble-display`.

## What The Firmware Does

At boot, the ESP32-P4:

1. Turns HV off.
2. Programs the iCE40 FPGA from `main/fpga.bin`.
3. Starts the FPGA runtime clock.
4. Initializes the DACx578 startup values.
5. Enables HV to `0xea`.
6. Waits 10 seconds before counting, so startup noise is not logged.
7. Mounts the SD card and creates a fresh data file.
8. Starts a Wi-Fi access point for setup, status, downloads, and controls.
9. Starts the independent USB-UART flight-monitor protocol.
10. Starts BLE live-count advertisements for the S3 screen display.
11. Automatically turns Wi-Fi off after the five-minute setup window unless **Keep Wi-Fi On** was selected, while UART monitoring, SD logging, counting, and BLE live display continue.

## Optional S3-GEEK Display

Flash the optional ESP32-S3-GEEK BLE display:

```sh
cd display/s3-geek-ble-display
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem11301 flash monitor
```

## Guides

- [Printable Detector Quick Guide (PDF)](docs/gLOWCOST_Detector_Quick_Guide.pdf)
- [Detector Operation Guide](docs/operation-guide.md)
- [Detector Sections Explained](docs/detector-sections.md)
- [Build, Flash, And Development Guide](docs/developer-guide.md)
- [Data Files And SD Logging](docs/data-format.md)
- [Threshold Tuning Results](docs/threshold-tuning-results.md)
- [Troubleshooting](docs/troubleshooting.md)

## Repository Layout

```text
CMakeLists.txt
sdkconfig
sdkconfig.defaults
dependencies.lock
main/
  CMakeLists.txt
  idf_component.yml
  app_common.h
  app_state.c
  console.c
  counters.c
  environment.c
  hardware.c
  main.c
  storage.c
  web.c
  fpga.bin
display/
  s3-geek-ble-display/
    CMakeLists.txt
    sdkconfig.defaults
    main/
      CMakeLists.txt
      main.c
docs/
  data-format.md
  detector-sections.md
  developer-guide.md
  kicadSCHEMATIC.pdf
  operation-guide.md
  threshold-tuning-results.md
  troubleshooting.md
```

Generated folders such as `build/` and `managed_components/` are intentionally ignored.

## Current Status

The latest cleaned firmware removes temporary debug diagnostics used during tuning. It keeps SD logging, BME280 environment logging, temperature compensation, web setup, FPGA flashing, HV protection, power-saving behavior, and the BLE live display path.
