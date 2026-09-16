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
- Wi-Fi and HTTP server stop.
- HV stays off for 3 seconds.
- HV is restored.
- The detector waits the normal 10-second settle time.
- Counting and SD logging continue.

Wi-Fi remains off until the next reset or power cycle.

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

If time was not synced before the file was created, the filename starts with `unsynced`.

## Interpreting A Quick Count Check

The main count channels are:

- `ch01_p13`: CH0 and CH1 coincidence
- `ch02_p12`: CH0 and CH2 coincidence
- `ch12_p11`: CH1 and CH2 coincidence
- `ch012_p22`: CH0, CH1, and CH2 triple coincidence
- `gpio6_p31`: raw CH0-like output
- `gpio5_p29`: raw CH1-like output
- `gpio16_p36`: raw CH2-like output

During threshold tuning, very low threshold values caused raw channels to rise into thousands per minute. Threshold `0x070` gave raw channels around the low hundreds per minute in the tested setup. This is a working threshold, but waveform captures still need to be analyzed further before calling the threshold fully optimized.

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
