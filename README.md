# Guillemot — Ninebot G30 BLE Immobilizer Receiver

Guillemot is the **deck receiver** module of a three-part immobilizer system ([Uguisu](https://github.com/LPFchan/Uguisu) fob + Guillemot receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. Guillemot validates short **encrypted BLE advertisements** from the fob and controls an **SR latch** that gates battery-to-ESC power via an inline XT60 splice.

This repository contains the **Guillemot receiver firmware and hardware design files**. Note that shared protocol and cryptography logic is implemented in the [ImmoCommon](https://github.com/LPFchan/ImmoCommon) submodule.

## Hardware

Guillemot uses male/female XT60 pigtails to sit inline with the main battery cable, requiring no permanent modifications to the scooter.

- **MCU**: Seeed Studio XIAO nRF52840
- **Power Budget**: ~22 μA total when locked (TPSM365R6V3 buck converter + 1% duty-cycled BLE scanning).
- **Control**: Dual NOR SR Latch (`D0=SET`, `D1=RESET`) drives a pre-charge FET (AO3422), an RC delay (100 ms), and the main low-side MOSFET (IPB042N10N3G, 100V/137A).
- **Audio**: 4 kHz SMD buzzer (`D3` PWM) for lock/unlock feedback.
- **PCB Design**: KiCad (`Guillemot.kicad_sch` / `Guillemot.kicad_pcb`). 4-layer, 54×34 mm board with 2 oz outer copper. Requires an external 12 AWG silicone wire linking the `BAT-` XT60 pad directly to the MOSFET Source pad.

## Firmware

The firmware is a [PlatformIO](https://platformio.org/) project located in `firmware/guillemot/`.
- **Scanning**: Uses duty-cycled scanning (20 ms scan every 2 seconds) to conserve power.
- **Validation**: Rejects payloads with invalid AES-CCM MICs or counters less than or equal to the last seen counter (anti-replay).

## Provisioning

Guillemot must be initialized with the [Whimbrel](https://github.com/LPFchan/Whimbrel) web app.
- Connect the receiver via USB and use Whimbrel to inject a shared AES-128 key over Web Serial.
- This enforces physical presence, preventing over-the-air pairing interception.
- The key and anti-replay counter log are stored persistently in internal flash. Firmware is compiled without any serial commands capable of reading the key back to the host.

## Protocol

- **BLE**: Advertisement-based. Listens for Manufacturer Specific Data containing a 2-byte device ID, 4-byte monotonic counter, 1-byte command (0x01=unlock, 0x02=lock), and 4-byte AES-CCM Message Integrity Code (MIC).
- **Shared Library**: Payload validation and cryptography are handled by [ImmoCommon](https://github.com/LPFchan/ImmoCommon).

## Safety & Notes

- **Safety**: This is a prototype security and power-interrupt device. Use at your own risk. Do not test lock behavior while riding.
- **Legal**: Not affiliated with Segway-Ninebot.
- **Repo**: Please avoid committing KiCad per-user state (`*.kicad_prl`) or lock files.
