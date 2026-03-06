# Uguisu — Ninebot G30 BLE Immobilizer Fob

Uguisu is the **BLE key fob** module of a three-part immobilizer system (Uguisu fob + [Guillemot](https://github.com/LPFchan/Guillemot) receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. It broadcasts short, encrypted BLE advertisements on button press to unlock or lock the scooter.

This repository contains the **Uguisu fob firmware and hardware design files**. Note that shared protocol and cryptography logic is implemented in the [ImmoCommon](https://github.com/LPFchan/ImmoCommon) submodule.

## Hardware

- **MCU**: Seeed Studio XIAO nRF52840 (Castellated, 21×17.5 mm)
- **Power**: 100 mAh 3.7 V LiPo soldered to the XIAO bottom pads. Uses < 5 μA in standby (12–18 months between charges).
- **Input**: Side-actuated SMD tact switch.
- **PCB Design**: KiCad (`Uguisu.kicad_sch` / `Uguisu.kicad_pcb`).

## Firmware

The firmware is a [PlatformIO](https://platformio.org/) project located in `firmware/uguisu/`.
- **Boot Routine**: Checks `VBUSDETECT`. If USB is connected, it enters Whimbrel Provisioning Mode. If battery powered, it initializes BLE, reads/increments the counter in NVS, generates the AES-128-CCM payload, broadcasts for ~2 seconds, and then enters deep sleep (`sd_power_system_off`).
- **Locking**: A lock command requires a 2-second long press.

## Protocol

- **BLE**: Advertisement-based with no persistent connection. Broadcasts Manufacturer Specific Data containing a 2-byte device ID, 4-byte monotonic anti-replay counter, 1-byte command (0x01=unlock, 0x02=lock), and 4-byte AES-CCM Message Integrity Code (MIC).
- **Shared Library**: Payload generation and cryptography are handled by [ImmoCommon](https://github.com/LPFchan/ImmoCommon).

## Onboarding

Uguisu is initialized with the [Whimbrel](https://github.com/LPFchan/Whimbrel) web app.
- **Firmware Flashing**: Whimbrel also features a web-based firmware flasher. You can flash the latest release directly from your browser via Web Serial by placing the device in Bootloader mode (double-tap reset).
- **Key Provisioning**: Connect the fob via USB and use Whimbrel to inject a shared AES-128 key over Web Serial. This enforces physical presence, preventing over-the-air pairing interception. The key and anti-replay counter are stored persistently in internal flash (NVS/FDS). Firmware is compiled without any serial commands capable of reading the key back to the host.

## Safety & Notes

- **Safety**: This is a prototype security device. Use at your own risk. Do not test lock behavior while riding.
- **Legal**: Not affiliated with Segway-Ninebot.
- **Repo**: Please avoid committing KiCad per-user state (`*.kicad_prl`) or lock files.