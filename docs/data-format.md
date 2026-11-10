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

If the SD card is mounted before browser time is known, the firmware starts
with an `unsynced` filename and renames the active muon and environment files
using the browser's local clock timestamp. If time sync happens while the SD
card is still mounting, the next browser sync retries the rename.

## Muon CSV Columns

```csv
epoch,iso,ch12_p13,ch13_p12,ch23_p11,ch1_p31,ch2_p29,ch3_p36
```

| Column | Meaning |
| --- | --- |
| `epoch` | Unix time in seconds since 1970-01-01 UTC |
| `iso` | browser-local readable timestamp after time sync, or `unset` before sync |
| `ch12_p13` | CH1 and CH2 coincidence |
| `ch13_p12` | CH1 and CH3 coincidence |
| `ch23_p11` | CH2 and CH3 coincidence |
| `ch1_p31` | CH1 single count |
| `ch2_p29` | CH2 single count |
| `ch3_p36` | CH3 single count |

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
