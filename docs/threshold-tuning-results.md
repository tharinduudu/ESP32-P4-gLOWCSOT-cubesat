# Threshold Tuning Results

This file records threshold DAC observations from the ESP32-P4 test session.

The threshold DAC channels are DACx578 channels 4-7. The current firmware startup value is:

```text
0x070
```

## Test Conditions

- HV byte: `0xea`
- SiPM DAC channels 0-3: `0x2f1`
- FPGA bitstream: `top_50MHz_led100_dt200_pi10us.bin`
- Count period: 60 seconds
- First row after boot/power-save settle was used for quick comparison

## Observed Rows

### Threshold `0x040`

```text
ch01_p13=4
ch02_p12=5
ch12_p11=5
ch012_p22=0
gpio6_p31=3356
gpio5_p29=3239
gpio16_p36=4822
```

Result: too noisy. Raw channels were in the thousands per minute.

### Threshold `0x080`

```text
ch01_p13=4
ch02_p12=2
ch12_p11=3
ch012_p22=0
gpio6_p31=68
gpio5_p29=51
gpio16_p36=78
```

Result: much quieter. Raw channels dropped to tens per minute.

### Threshold `0x070`

```text
ch01_p13=5
ch02_p12=3
ch12_p11=3
ch012_p22=0
gpio6_p31=113
gpio5_p29=106
gpio16_p36=146
```

Result: slightly noisier than `0x080`, but still far quieter than `0x040`.

## Current Choice

`0x070` was left as the active threshold value because it is between the quiet `0x080` setting and the overly sensitive `0x040` setting.

For future tuning, take longer runs at each threshold. One minute is useful for quick checks, but stable detector tuning should compare longer periods under similar temperature and scintillator conditions.

The present threshold choice should be treated as a practical operating setting, not a final optimized value. Waveforms from the analog/front-end outputs should be captured and analyzed further to fine-tune the threshold against real pulse height, noise pickup, afterpulsing, and coincidence efficiency.
