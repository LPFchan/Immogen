# Uguisu — Ninebot G30 BLE Immobilizer Fob

Uguisu is the BLE key fob in a three-part immobilizer system ([Guillemot](https://github.com/LPFchan/Guillemot) receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. It broadcasts short, encrypted BLE advertisements on button press to unlock or lock the vehicle. This repository contains the fob firmware and hardware design files. Protocol and cryptography via [ImmoCommon](https://github.com/LPFchan/ImmoCommon).

---

## Hardware

### Components & BOM (~$10)

| Ref   | Part               | Notes                         | Source       | Cost   |
| ----- | ------------------ | ----------------------------- | ------------ | ------ |
| U1    | XIAO nRF52840      | Castellated, 21×17.5 mm       | Seeed/Amazon | $6.00  |
| BAT   | 100 mAh 3.7 V LiPo | Solder pads on XIAO bottom    | Generic      | $2.00  |
| SW1   | Tact switch        | Side-actuated, SMD            | Generic      | $0.10  |
| —     | Enclosure          | ~35×25×12 mm, 3D-printed      | Local        | ~$1.00 |

Hand-solder XIAO, LiPo, and tact switch.

### PCB Design

KiCad (`Uguisu.kicad_sch` / `Uguisu.kicad_pcb`).

### Operation

- **Unlock flow:** Button press → GPIO wake → boot → init BLE → read/increment NVS → AES-128-CCM → broadcast ~2 s → sleep QSPI (0xB9) → `sd_power_system_off()`.
- **Lock flow:** 1-second long press → same sequence, command 0x02.
- **Power:** < 5 μA standby, ~0.004 mAh per press (12–18 months between charges on 100 mAh LiPo). Charging via USB-C.

### Design Notes

- **NVS wear:** Counter written every press (System OFF kills RAM). At 10 unlocks/day, ~2.7 years; wear-leveling extends this.

---

## Software

### Firmware

[PlatformIO](https://platformio.org/) project in `firmware/uguisu/`.

Boot routine checks `NRF_POWER->USBREGSTATUS` (`VBUSDETECT`). If USB connected: enters Whimbrel Provisioning Mode (waits for serial payload). If battery powered: full reset → BLE init → NVS read/increment/persist → AES-128-CCM → broadcast ~2 s → sleep QSPI flash (0xB9) → `sd_power_system_off()`. Next press wakes via GPIO (hardware SENSE).

### Protocol

- Advertisement-based BLE (no persistent connection).
- Payload: 4-byte monotonic counter, 1-byte command (0x01 = unlock, 0x02 = lock), 4-byte AES-CCM MIC.
- Generation and crypto via [ImmoCommon](https://github.com/LPFchan/ImmoCommon).

### Onboarding

Use [Whimbrel](https://github.com/LPFchan/Whimbrel) for both tasks:

- **Firmware flashing:** Web-based flasher via Web Serial. Enter bootloader mode with a double-tap on reset.
- **Key provisioning:** Connect via USB; Whimbrel injects the shared AES-128 key. Physical presence is required. The firmware has no serial commands to read the key back.

---

## Safety & Notes

- Prototype security device. Use at your own risk.
- Do not test lock behavior while riding.
- Not affiliated with Segway-Ninebot.
- Avoid committing KiCad per-user state (`*.kicad_prl`) or lock files.
