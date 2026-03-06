# Guillemot — Ninebot G30 BLE Immobilizer Receiver

Guillemot is the **deck receiver** module of a three-part immobilizer system ([Uguisu](https://github.com/LPFchan/Uguisu) fob + Guillemot receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. Guillemot validates short **encrypted BLE advertisements** from the fob and controls an **SR latch** that gates battery-to-ESC power (inline XT60 splice).

This repository contains the **Guillemot receiver firmware and hardware design files**. Note that shared protocol and cryptography logic is implemented in the [ImmoCommon](https://github.com/LPFchan/ImmoCommon) submodule.

## Hardware

- **MCU**: Seeed Studio XIAO nRF52840
- **Control Pins**: SR Latch (`D0=SET`, `D1=RESET`), Buzzer (`D3`)
- **Power**: Buck converter to 3.3V, duty-cycled BLE scanning while locked
- **PCB Design**: KiCad (`Guillemot.kicad_sch` / `Guillemot.kicad_pcb`)

## Firmware

The firmware is a [PlatformIO](https://platformio.org/) project located in `firmware/guillemot/`.
- Open the folder in VSCode/Cursor with the PlatformIO extension.
- Build and upload via USB.

## Provisioning

Guillemot must be initialized with the [Whimbrel](https://github.com/LPFchan/Whimbrel) web app.
- Connect the receiver via USB and use Whimbrel to inject a shared AES-128 key.
- The key and anti-replay counter log are stored persistently in internal flash.

## Protocol

- **BLE**: Listens for Manufacturer Specific Data containing a device ID, counter, command, and AES-CCM MIC.
- **Validation**: Rejects payloads with invalid MICs or counters less than or equal to the last seen counter (anti-replay).
- **Shared Library**: Validation and cryptography are handled by [ImmoCommon](https://github.com/LPFchan/ImmoCommon).

## Safety & Notes

- **Safety**: This is a prototype security and power-interrupt device. Use at your own risk. Do not test lock behavior while riding.
- **Legal**: Not affiliated with Segway-Ninebot.
- **Repo**: Please avoid committing KiCad per-user state (`*.kicad_prl`) or lock files.
