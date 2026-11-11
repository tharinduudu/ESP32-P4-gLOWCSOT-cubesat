# Detector Operation Guide

This guide describes how to use the ESP32-P4 gLOWCOST detector in the lab or in a field run.

## Before Power-On

Check the hardware:

- ESP32-P4 dev kit is seated on the readout board header.
- SD card is installed.
- Scintillator/SiPM connections are secure.
- BME280 is connected to the I2C port if environment logging is needed.
- USB or power bank can supply enough current.

The firmware creates a new data file each time the detector starts.

## Normal Startup

Power the ESP32-P4. The firmware performs this sequence:

1. HV off.
2. FPGA programmed.
3. DAC startup values written.
4. HV set to `0xea`.
5. 10-second HV settle delay.
6. Counting starts.
7. SD logging starts.
8. Wi-Fi setup page becomes available.

The 10-second wait is intentional. It prevents startup noise from being included in the first logged count period.

## Web Setup

Connect to:

- SSID: `MuonReadout`
- Password: `glowcost`
- URL: `http://192.168.4.1`

Use the page to:

- Confirm SD card readiness.
- View live minute counts.
- Sync time from the browser automatically.
- Add a run label for the SD filename.
- Download the active file or previous SD files.
- Set HV byte or turn HV off.
- Reflash the FPGA using the protected sequence.
- Reload startup DAC settings.
- Keep Wi-Fi on or allow auto-off.

The Wi-Fi setup window lasts five minutes from startup. A countdown is shown at
the top of the web page. The timer still expires if a phone or laptop is left
connected. Select **Keep Wi-Fi On** to disable automatic shutdown for the
current power cycle; select **Allow Auto-Off** to start a fresh five-minute
countdown.

## Wired UART Flight Monitor

The board's second Type-C connector is the USB-UART port. It can provide a wired live page without turning Wi-Fi back on.

1. Download the
   [packaged Mac monitor](https://github.com/tharinduudu/ESP32-P4-gLOWCSOT-cubesat/raw/refs/heads/main/dist/Muon-UART-Monitor-macOS.zip).
   The ZIP contains `uart_web_monitor.py`, the launcher, and its Read Me.
2. Unzip the package. Confirm Python 3 is installed using `python3 --version` in
   Terminal; install it from python.org before the flight if needed.
3. Keep the power bank connected and connect the separate USB-UART Type-C port
   to the Mac with a data cable.
4. In Terminal type `python3` followed by a space. Drag `uart_web_monitor.py`
   from the extracted folder into Terminal, then press Return. Keep Terminal open.
5. Open `http://127.0.0.1:8080` on the Mac and check the connection and time.

From a repository checkout, the equivalent command is:

```sh
python3 tools/uart_web_monitor.py --open-browser
```

The local page shows the latest 10 count lines, highlights the newest line,
shows detector status, syncs time from the browser, changes the run label,
performs protected controls, and downloads SD files. Disconnecting the Mac or
stopping the bridge does not stop counting or SD logging. The detector remains
powered by the power bank.

The Terminal method above does not require opening the `.command` launcher. If
the page reports that UART is unavailable, close `screen`, miniterm, or another
program that may be using the same serial port. Check for a port with:

```sh
ls /dev/cu.usbmodem*
```

## BLE Quick-Look Display

For low-noise field checks, use the ESP32-S3-GEEK display instead of keeping the Wi-Fi page open. The P4 broadcasts live count snapshots over BLE, and the S3 screen passively listens.

The display shows:

- live `1-2`, `1-3`, and `2-3` coincidence counts
- single-channel `CH1`, `CH2`, and `CH3` counts
- HV byte and counting state
- SD card status
- FPGA and time-sync status
- stale-packet warning if the P4 is not heard

The display is only a quick readout. The SD card files on the P4 are still the scientific record.

## Run Label

The run label is appended to the SD filename. Examples:

- `lab_test`
- `flight_A12`
- `campus_roof`
- `balloon_2026_09`

Spaces and unsafe filename characters are converted to underscores.

## Power-Saving Mode

For power-bank operation, avoid leaving Wi-Fi on.

Recommended field sequence:

1. Boot detector.
2. Connect to `MuonReadout`.
3. Open the web page.
4. Confirm SD is ready.
5. Let browser time sync.
6. Set a run label.
7. Confirm counts look reasonable.
8. Press **Turn Wi-Fi Off**, or simply let auto-off happen.

When Wi-Fi is turned off:

- HV is turned off first.
- Wi-Fi and its onboard HTTP server stop; the independent UART monitor remains available.
- HV stays off for 3 seconds.
- HV is restored.
- The detector waits the normal 10-second settle time.
- Counting and SD logging continue.
- The wired UART monitor remains available.
- BLE live display advertisements continue.

Wi-Fi remains off until the next reset or power cycle. Use the wired UART dashboard on the Mac while Wi-Fi is off.

## End Of Run

Power down the detector before removing the SD card. Then copy the files from the card.

Muon count files look like:

```text
muon_YYYYMMDD_HHMMSS_label.csv
```

Environment files look like:

```text
env_YYYYMMDD_HHMMSS_label.csv
```

If time was not synced before the file was created, the filename starts with `unsynced`. Once browser time sync succeeds, the active muon and environment files are renamed with the browser's local clock timestamp.

## Interpreting A Quick Count Check

The main count channels are:

- `ch12_p13`: CH1 and CH2 coincidence
- `ch13_p12`: CH1 and CH3 coincidence
- `ch23_p11`: CH2 and CH3 coincidence
- `ch1_p31`: CH1 single count
- `ch2_p29`: CH2 single count
- `ch3_p36`: CH3 single count

The FPGA also places spare CH0 singles on BCM25, but the CH1/CH2/CH3 detector
profile does not record that line.

During threshold tuning, very low threshold values caused raw channels to rise into thousands per minute. Threshold `0x080` was the quietest quick-test setting, while `0x070` was slightly noisier but still usable. Waveform captures still need to be analyzed further before calling the threshold fully optimized.

## FPGA Reflash Procedure

Use the web button only after the detector is in a safe state.

The firmware sequence is:

1. HV off.
2. FPGA flash.
3. Startup DAC values loaded.
4. HV restored.
5. 10-second settle.
6. Counting resumes.

This avoids flashing the FPGA while the detector is biased.
