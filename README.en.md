# K5RX Firmware

K5RX is a custom firmware that turns **Quansheng UV-K5 / UV-K6 V1 radios using the DP32G030 MCU** into a receiver-focused platform.

> [日本語 README](README.md)

It removes normal transmit operation in software and focuses the interface and feature set on receiving and signal discovery: 400 memories, banks, Fast Scan, Spectrum Analyzer, Broadcast FM and more.

## Supported hardware

Supported:

- Quansheng UV-K5 V1 family
- Quansheng UV-K6 V1 family
- DP32G030 MCU

Not supported:

- UV-K5 V3 family
- UV-K1 family
- variants using a different MCU/platform

**Do not flash K5RX to unsupported hardware.**

## What you can do with K5RX

### Receiver-focused operation

- Normal VFO and memory reception
- **Monitor** while PTT is held
- TINY / CLASSIC GUI
- RX Timer

K5RX does not provide normal transmit operation. On the main receiver screen, PTT acts as momentary Monitor. Releasing it returns to the previous receive state; if scanning was active, scanning resumes.

### 400 memories and scanning

- 400 memory channels
- 10-character memory names
- Three Scan Lists
- Six scan targets:
  - channels in no Scan List
  - Scan List 1
  - Scan List 2
  - Scan List 3
  - channels in any of Lists 1–3
  - all valid memory channels
- Normal Scan and Adaptive Fast Scan

Fast Scan spends less time passing over quiet channels. While scanning, the main screen displays the measured scan rate in `ch/s`.

### Eight banks

The 400 memories can be organized into eight banks. The Bank screen lets you browse memories in a bank, listen to a selected memory and change its membership in Scan Lists 1–3.

Each memory can belong to at most one bank. Bank membership and Scan List membership are independent, so they can be combined for different ways of organizing and scanning memories.

### Receiver utilities

- Broadcast FM radio
- Spectrum Analyzer
- F4HWN-derived receive features, including AM reception

See [`docs/features.md`](docs/features.md) for user-facing operation details.

## First-time installation

K5RX uses a different EEPROM format from stock firmware and normal F4HWN builds. **Back up the complete EEPROM before flashing K5RX.**

Recommended procedure:

1. Save the current EEPROM as an 8192-byte RAW backup.
2. Flash K5RX firmware.
3. Confirm that the radio boots as K5RX.
4. Perform a **Full Factory Reset** to initialize the K5RX EEPROM layout.
5. Configure memories and banks on the radio or with K5RX Tools.
6. Verify the receive, Scan, Fast Scan and other functions you normally use.

Read [`docs/eeprom-migration.md`](docs/eeprom-migration.md) before migrating or restoring EEPROM data.

## Editing memories from a computer

The companion project **K5RX Tools (`k5rx-tools`)** manages K5RX memories and banks from a computer.

Typical uses include:

- RAW EEPROM backup
- CSV import/export for all 400 memories
- bank names and bank membership
- a browser-based Web Serial Memory Manager
- CLI read / edit / write / verify workflows

See [`docs/compatibility.md`](docs/compatibility.md) for the firmware/tools compatibility contract.

## What “receive-only” means

The standard K5RX build removes or disables transmit screens, transmit processing and PA control in software, and also includes lower-level guards intended to make TX/PA activation harder to reach accidentally.

This is **software removal of transmit functionality**, not a certified guarantee of zero RF emission. It cannot cover hardware faults, unknown silicon behavior or external modifications. Follow the laws and operating requirements that apply where you use the radio.

See [`docs/technical-overview.md`](docs/technical-overview.md) for the technical design.

## EEPROM compatibility

K5RX uses **K5RX EEPROM Schema 2**.

| Item | K5RX Schema 2 |
|---|---:|
| EEPROM size | 8192 bytes |
| Memory channels | 400 |
| Channel record | 8 bytes |
| Channel name | 10 bytes |
| Banks | 8 |
| K5RX settings area | `0x0000..0x1DFF` |
| Factory / Calibration | `0x1E00..0x1FFF` |

A stock or normal F4HWN EEPROM cannot be used directly as K5RX settings. Likewise, when returning to another firmware, restore an EEPROM backup or use a migration procedure appropriate to that firmware.

Normal K5RX settings writes and K5RX Tools memory editing are designed not to write the Factory / Calibration region.

## Building from source

K5RX is the default build profile:

```bash
make clean all
```

For Docker or Apple's `container` CLI:

```bash
sh ./build.sh
```

Output:

```text
compiled-firmware/
├── k5rx-firmware.bin
├── k5rx-firmware.packed.bin
└── SHA256SUMS
```

See [`docs/build.md`](docs/build.md).

## Documentation

- [`docs/features.md`](docs/features.md) — main features and controls
- [`docs/eeprom-migration.md`](docs/eeprom-migration.md) — EEPROM backup, first install and recovery
- [`docs/compatibility.md`](docs/compatibility.md) — hardware, EEPROM and tools compatibility
- [`docs/technical-overview.md`](docs/technical-overview.md) — receive-only design, EEPROM, Scan and Bank architecture
- [`docs/build.md`](docs/build.md) — source/container build
- [`docs/flash-size-policy.md`](docs/flash-size-policy.md) — flash-size constraints for firmware contributors

## Origin / Credits

K5RX is based directly on F4HWN firmware and builds on the wider UV-K5 open-firmware ecosystem, including work by F4HWN / armel, Egzumer, OneOfEleven, DualTachyon, fagci and many other contributors.

The K5RX Git history starts from F4HWN v4.3 commit `fbcf26d8e9811b135b7e2d97bdefebaa4b3ed9e0`. See [`NOTICE`](NOTICE) for attribution details.

## License

Apache License 2.0. See [`LICENSE`](LICENSE).
