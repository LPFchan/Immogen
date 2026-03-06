# Uguisu (BLE fob) — Ninebot G30 immobilizer

Uguisu is the **BLE key fob** module for a two-module immobilizer system:

- **Uguisu (this repo)**: short, encrypted BLE advertisements on button press
- **Guillemot (separate repo)**: scooter deck receiver that validates adverts and gates battery-to-ESC power

## Contents

- KiCad project files:
  - `Uguisu.kicad_sch`
  - `Uguisu.kicad_pcb`

## Firmware (PlatformIO)

Firmware lives in `firmware/uguisu/` and targets the Seeed XIAO nRF52840 (Bluefruit).

### Build / upload

- Install PlatformIO (VSCode/Cursor extension)
- Open `firmware/uguisu/` as a PlatformIO project
- Use **Build** / **Upload**

### Secrets

Create `firmware/uguisu/include/uguisu_secrets_local.h` (gitignored) and define your 16-byte PSK:

```c
#define UGUISU_PSK_BYTES \
  0x01, 0x02, 0x03, 0x04,   \
  0x05, 0x06, 0x07, 0x08,   \
  0x09, 0x0A, 0x0B, 0x0C,   \
  0x0D, 0x0E, 0x0F, 0x10
```

### Protocol compatibility (Uguisu ↔ Guillemot)

The fob transmits manufacturer-specific data containing:

- `company_id(2) | payload(11)`
- `payload(11) = device_id(2) | counter(4) | command(1) | mic(4)`

The MIC is a 4-byte AES-CCM tag using:

- `nonce(13) = le16(device_id) | le32(counter) | 0x00*7`
- `msg(7) = le16(device_id) | le32(counter) | command`

## Test vectors

See `tools/test_vectors/` for a small script that generates expected MIC values for interoperability testing.

## Notes

- This repo intentionally avoids committing KiCad per-user state (`*.kicad_prl`) and backup/lock artifacts.
- Safety: do not test power-interrupt/lock behavior while riding.

