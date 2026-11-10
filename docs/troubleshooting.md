# Troubleshooting

## Wi-Fi Network Not Visible

Expected network:

```text
SSID: MuonReadout
Password: glowcost
```

The AP remains available for five minutes after startup and then turns off even if a phone was left connected. Select **Keep Wi-Fi On** before the countdown reaches zero when longer access is required. Reset or power-cycle the ESP32-P4 to restart the setup window.

If you want more time on the web UI, connect quickly and press **Keep Wi-Fi On**.

## SD Card Not Ready

Check:

- SD card is inserted.
- Card is formatted FAT-compatible.
- Card contacts are clean.
- Board is power-cycled after inserting the card.

The web UI highlights SD card readiness at the top of the page.

## BME280 Not Found

Check:

- BME280 power is 3.3 V.
- GND is common with ESP32-P4/readout board.
- SDA is on Pi physical pin 3 / ESP32-P4 GPIO7.
- SCL is on Pi physical pin 5 / ESP32-P4 GPIO8.
- Address is `0x76` or `0x77`.

If the BME wiring holds SCL or SDA low, the DACx578 may also fail because it is on the same I2C bus. The firmware will keep HV off if DAC startup fails.

## DAC Startup Fails

If serial log shows DAC timeout or startup HV remains off:

- Check I2C wiring.
- Check DACx578 address `0x47`.
- Check BME280 wiring, because a bad BME connection can hold the whole I2C bus low.
- Reset after fixing wiring.

The firmware intentionally leaves HV off if DAC startup fails.

## FPGA Not Programmed

Healthy boot should include FPGA DONE high and FPGA program OK.

If FPGA programming fails:

- Check SPI pins.
- Check FPGA reset and DONE wiring.
- Confirm `main/fpga.bin` is present.
- Use the web FPGA reflash button, which turns HV off before programming.

## Counts Are Extremely High

Possible causes:

- Threshold too low.
- Wi-Fi still on and coupling noise.
- HV startup noise was included because data was inspected before settle.
- SiPM/scintillator disconnected or noisy.
- FPGA bitstream mismatch.

Threshold observations:

- `0x040` was too noisy in the test session.
- `0x080` was quiet.
- `0x080` is the current conservative setting.

## Counts Are Zero

Check:

- HV is enabled.
- HV settle delay has completed.
- FPGA DONE is true.
- Scintillators/SiPMs are connected.
- Threshold is not too high.
- The detector is not in HV-off state after FPGA flashing.

## No Serial Minute Rows After Power Save

This is expected. To save power and reduce unnecessary activity, serial minute count printing is suppressed after power-saving mode starts. Data continues logging to SD.

Use the web UI before Wi-Fi turns off, or read the CSV file from the SD card after the run.

## Wired UART Page Is Unavailable

The USB-UART flight monitor is different from the native USB maintenance console.

1. Keep the power bank connected to the normal power Type-C port.
2. Connect the other Type-C, marked USB-UART, to the Mac.
3. Confirm a port exists with `ls /dev/cu.usbmodem*`.
4. Run `python3 tools/uart_web_monitor.py --port /dev/cu.usbmodem...` from the repository root.
5. Open `http://127.0.0.1:8080`, not `192.168.4.1`.

If the page says UART unavailable, close `screen`, `miniterm`, or any other program using the same serial port. The bridge can be stopped and restarted without interrupting the detector run.
