# Guillemot (Receiver) — BLE Immobilizer for Ninebot Max G30

Guillemot is the **deck receiver** module of a two-module immobilizer system (Uguisu fob + Guillemot receiver) for the Ninebot Max G30. Guillemot validates short **encrypted BLE advertisements** from the fob and controls an **SR latch** that gates battery-to-ESC power (inline XT60 splice).

This repository currently focuses on **Guillemot receiver firmware + hardware design files**.

## Safety / legal

- This is a prototype security/power-interrupt device. Use at your own risk.
- Not affiliated with Segway-Ninebot.
- Do not test “lock” behavior while riding.

## Hardware summary (receiver)

- **MCU**: Seeed XIAO nRF52840
- **Latch control GPIO**: `D0=SET`, `D1=RESET`
- **Buzzer PWM**: `D3`
- **Power**: buck to 3.3 V, duty-cycled scanning while locked

## BLE protocol (payload)

Advertisement-carried payload (11 bytes):

| Field | Size |
|------|------|
| Device ID | 2 B |
| Counter | 4 B |
| Command | 1 B |
| MIC | 4 B |

Commands: `0x01=unlock`, `0x02=lock`. Receiver accepts only `counter > last_seen_counter` (anti-replay).

## Firmware (PlatformIO)

Firmware lives in `firmware/guillemot/`.

### Prereqs

- Install PlatformIO (Cursor/VSCode extension).
- Hardware: Seeed XIAO nRF52840 connected over USB.

### Build / upload

Open `firmware/guillemot/` in PlatformIO and use **Build** / **Upload**.

## Secrets

Create `firmware/guillemot/include/guillemot_secrets_local.h` (gitignored) and define your PSK bytes there.

Example:

```c
#define GUILLEMOT_PSK_BYTES \
  0x01, 0x02, 0x03, 0x04,   \
  0x05, 0x06, 0x07, 0x08,   \
  0x09, 0x0A, 0x0B, 0x0C,   \
  0x0D, 0x0E, 0x0F, 0x10
```

