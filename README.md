# Uguisu — Ninebot G30 BLE Immobilizer Fob

Uguisu is the **BLE key fob** module of a three-part immobilizer system (Uguisu fob + Guillemot receiver + Whimbrel web app) for the Ninebot Max G30. Uguisu transmits short, **encrypted BLE advertisements** on button press to unlock or lock the scooter.

This repository contains the **Uguisu fob firmware + hardware design files**.

## Hardware / Tech Stack

- **MCU**: Seeed XIAO nRF52840 (Bluetooth Low Energy)
- **Power**: Button cell or LiPo (via XIAO)
- **PCB Design**: KiCad

## Firmware / Usage

Firmware lives in `firmware/uguisu/` and targets the Seeed XIAO nRF52840 (Bluefruit).

### Prerequisites

- Install PlatformIO (VSCode/Cursor extension)
- Hardware: Seeed XIAO nRF52840 connected over USB.

### Build / Upload

Open `firmware/uguisu/` as a PlatformIO project and use **Build** / **Upload**.

## Provisioning & Protocol

Uguisu initializes with **Whimbrel** ([github.com/LPFchan/Whimbrel](https://github.com/LPFchan/Whimbrel)), a browser-based provisioning app that injects the same AES-128 key into both the fob and receiver over Web Serial.

1. Open Whimbrel in Chrome or Edge, generate a secret.
2. Plug the **fob** (Uguisu) into the PC via USB-C.
3. In Whimbrel, click **Flash Key Fob**. Select the Uguisu serial port when prompted.

When the board is powered over USB (VBUS present), it waits up to **30 seconds** for a provisioning payload via serial:
`PROV:UGUISU_01:<32-hex-key>:00000000:<4-hex-CRC>`

It stores the 16-byte key in internal flash (`/ug_prov.bin`) and resets its transmission counter log to `0`. If `/ug_prov.bin` already exists, the fob uses that key; otherwise, it uses a compile-time default (zeros).

### BLE Protocol

The fob transmits manufacturer-specific data containing:
`company_id(2) | payload(11)`

The payload is 11 bytes:
- **Device ID**: 2 Bytes
- **Counter**: 4 Bytes (Anti-replay incremented per button press)
- **Command**: 1 Byte (`0x01=unlock`, `0x02=lock`)
- **MIC**: 4 Bytes (AES-CCM Message Integrity Code)

## Test Vectors

See `tools/test_vectors/` for a small script that generates expected MIC values for interoperability testing. Note: This repo avoids committing KiCad per-user state (`*.kicad_prl`).

## Safety & Legal

- This is a prototype security/power-interrupt device. Use at your own risk.
- Not affiliated with Segway-Ninebot.
- **Do not test “lock” behavior while riding.**
