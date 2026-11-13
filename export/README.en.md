# OpenDMR — Baofeng DM-32 / DM-32UV

## Contents

| File | What for |
|---|---|
| `opendmr.bin` | Firmware to flash with OpenDMR already running (radio on, cable plugged) |
| `opendmr_BFUV32.bin` | Firmware for bootloader mode (PTT + SK1 while powering on) |
| `fw_update.py` | Update over the cable (radio on, ~35 s) |
| `dm32_dump.py` | Backup/write through the bootloader (radio off → power on within 30 s) |
| `write_keps.py` | Write amateur satellite keps (needs OpenGD77 header) |
| `manual.en.md` / `manual.es.md` | User manual |
| `installation.en.md` / `installation.es.md` | Installation guide |
| `README.md` / `README.en.md` | Quick instructions |
| `version.txt` | Git commit and date |

## What this build has

- **GPS rx**: receives the caller's position (ETSI GPS Info) and shows **distance and bearing** from you on the receive screen, and saves it in *Last heard*.
- **Spanish** as language 2 (English 0 / Chinese 1 / Spanish 2).
- **P25** (mode `SK1 + *`, cycle FM→AM→USB→LSB→DMR→P25).
- **APRS beacon now** (`SK2 + 2` or menu *Options → APRS → Send beacon now*) and **@** mark on the bottom bar when the beacon is on.

> ⚠️ The GPS driver is not written yet, so the radio **does not get its own position automatically**. Type it in *Radio info → Location* to see distance + bearing (without your position it shows only the caller's coordinates).

## How to flash

### If the radio already runs OpenDMR (radio on, cable plugged):

```powershell
py -u fw_update.py opendmr.bin --port COM3
```

### If it does not run or has no OpenDMR:

Power off the radio, hold **PTT + SK1** (side key under the PTT) and power on (green LED). Then:

```powershell
py -u dm32_dump.py COM3 program opendmr_BFUV32.bin --write --start 0xC000
```

## How to test

1. Menu → *Options → Language* → cycle to **Español**.
2. On a DMR receive from another radio sending GPS, you'll see distance and bearing from you (e.g. `12.3 km NE`).
3. *Last heard* keeps the position and the TG of the caller.
4. P25: **SK1 + `*`** to change mode.
5. APRS: on an FM channel with `aprs_config_index`, **SK2 + 2** sends a beacon.

**SHA256:** use `certutil -hashfile opendmr.bin SHA256` on Windows to confirm the download.
