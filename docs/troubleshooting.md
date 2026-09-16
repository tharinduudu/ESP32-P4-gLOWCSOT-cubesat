# Troubleshooting

## Wi-Fi Network Not Visible

Expected network:

```text
SSID: MuonReadout
Password: glowcost
```

The AP is intended for setup and may auto-off when no client connects. Reset or power-cycle the ESP32-P4 to restart the setup window.

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
- `0x070` is the current compromise.

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
