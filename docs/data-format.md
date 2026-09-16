# Data Files And SD Logging

The firmware creates fresh files on the SD card for each detector start.

## Muon Count File

Before time sync:

```text
/sdcard/muon_unsynced_<uptime_seconds>.csv
```

After time sync:

```text
/sdcard/muon_YYYYMMDD_HHMMSS.csv
```

With a run label:

```text
/sdcard/muon_YYYYMMDD_HHMMSS_<label>.csv
```

If the filename already exists, a suffix such as `_01` is added.

## Muon CSV Columns

```csv
epoch,iso,ch01_p13,ch02_p12,ch12_p11,ch012_p22,gpio6_p31,gpio5_p29,gpio16_p36
```

| Column | Meaning |
| --- | --- |
| `epoch` | Unix time in seconds since 1970-01-01 UTC |
| `iso` | readable timestamp |
| `ch01_p13` | CH0 and CH1 coincidence |
| `ch02_p12` | CH0 and CH2 coincidence |
| `ch12_p11` | CH1 and CH2 coincidence |
| `ch012_p22` | triple coincidence |
| `gpio6_p31` | raw channel output |
| `gpio5_p29` | raw channel output |
| `gpio16_p36` | raw channel output |

One row is written every 60 seconds while counting is enabled.

No row is written while:

- HV is off
- HV is settling
- the detector is still in the startup settle delay

## Environment File

The environment file stores 5-minute BME280 averages:

```csv
epoch,iso,samples,temp_c_avg,pressure_hpa_avg,humidity_pct_avg
```

The BME280 is optional. If it is not present, muon counting still works.

## Downloading Data

From the web interface:

- **Download Current File** gets the active muon file.
- **Download Environment File** gets the active environment file.
- **Previous SD Files** lists old `.csv` and `.log` files newest first.

The newest live minute row is also available at:

```text
http://192.168.4.1/api/latest.txt
```
