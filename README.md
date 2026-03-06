# Guillemot — Ninebot G30 BLE Immobilizer Receiver

Guillemot is the **deck receiver** module of a three-part immobilizer system ([Uguisu](https://github.com/LPFchan/Uguisu) fob + Guillemot receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. Guillemot validates short **encrypted BLE advertisements** from the fob and controls an **SR latch** that gates battery-to-ESC power (inline XT60 splice).

This repository contains the **Guillemot receiver firmware + hardware design files**. Note that shared protocol and cryptography logic is implemented in the [ImmoCommon](https://github.com/LPFchan/ImmoCommon) submodule.

## Hardware / Tech Stack

- **MCU**: Seeed XIAO nRF52840 (Bluetooth Low Energy)
- **Latch control GPIO**: `D0=SET`, `D1=RESET`
- **Buzzer PWM**: `D3`
- **Power**: buck to 3.3 V, duty-cycled scanning while locked
- **PCB Design**: KiCad

## Firmware / Usage

Firmware lives in `firmware/guillemot/`.

### Prerequisites

- Install PlatformIO (Cursor/VSCode extension).
- Hardware: Seeed XIAO nRF52840 connected over USB.

### Build / Upload

Open `firmware/guillemot/` in PlatformIO and use **Build** / **Upload**.

## Provisioning & Protocol

Guillemot initializes with **Whimbrel** ([github.com/LPFchan/Whimbrel](https://github.com/LPFchan/Whimbrel)), a browser-based provisioning app that injects the same AES-128 key into both the fob and receiver over Web Serial.

1. Open Whimbrel in Chrome or Edge, generate a secret, and flash the key fob (Uguisu).
2. Plug the **receiver** (Guillemot) into the PC via USB-C.
3. In Whimbrel, click **Flash Receiver**. Select the Guillemot serial port when prompted.

When the board is powered over USB (VBUS present), it waits up to **30 seconds** for a provisioning payload via serial:
`PROV:GUILLEMOT_01:<32-hex-key>:00000000:<4-hex-CRC>`

It stores the 16-byte key in internal flash (`/psk.bin`) and clears the anti-replay counter log.

### BLE Protocol
The advertisement-carried payload (11 bytes):
- **Device ID**: 2 Bytes
- **Counter**: 4 Bytes (Anti-replay: receiver only accepts `counter > last_seen_counter`)
- **Command**: 1 Byte (`0x01=unlock`, `0x02=lock`)
- **MIC**: 4 Bytes (AES-CCM Message Integrity Code)

## Safety & Legal

- This is a prototype security/power-interrupt device. Use at your own risk.
- Not affiliated with Segway-Ninebot.
- **Do not test “lock” behavior while riding.**
