# Uguisu — Ninebot G30 BLE Immobilizer Fob

Uguisu is the BLE key fob in a three-part immobilizer system ([Guillemot](https://github.com/LPFchan/Guillemot) receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. It broadcasts short, encrypted BLE advertisements on button press to unlock or lock the vehicle. This repository contains the fob firmware and hardware design files. Protocol and cryptography via [ImmoCommon](https://github.com/LPFchan/ImmoCommon).

---

## Hardware

### Components & BOM (~$10)


| Ref    | Part          | Notes                                 | Source / LCSC | Cost  |
| ------ | ------------- | ------------------------------------- | ------------- | ----- |
| U1     | XIAO nRF52840 | Castellated, 21×17.5 mm               | Seeed/Amazon  | $6.00 |
| BAT    | TW-502535     | 3.7V 400mAh LiPo                      | Generic       | $2.00 |
| KEY1   | SKQGABE010    | Tactile Switch SMD-4P 5.2x5.2mm       | C115351       | $0.11 |
| LED1   | XL-5050RGBC   | SMD5050-6P RGB LED Common Anode       | C2843868      | $0.05 |
| R1, R3 | 120 Ω         | 0603WAF1200T5E, 100mW 1% (Blue/Green) | C22787        | <$0.01|
| R2     | 330 Ω         | 0603WAF3300T5E, 100mW 1% (Red)        | C23138        | <$0.01|
| —      | Enclosure     | ~50×35×12 mm, 3D-printed              | Homemade      | —     |


Hand-solder XIAO and LiPo.

### PCB Design

KiCad (`Uguisu.kicad_sch` / `Uguisu.kicad_pcb`).

### GPIO


| Function      | Pin | nRF P0.x | Notes                                              |
| ------------- | --- | -------- | -------------------------------------------------- |
| Button (KEY1) | D0  | P0.02    | Active-low, internal pull-up, wake-from-system-off |
| Blue (LED1)   | D1  | P0.03    | LED1 pin 1, R1 120 Ω, active-low (sink)            |
| Red (LED1)    | D2  | P0.28    | LED1 pin 2, R2 330 Ω, active-low (sink)            |
| Green (LED1)  | D3  | P0.29    | LED1 pin 3, R3 120 Ω, active-low (sink)            |


### Operation

- **Unlock flow:** Button press → GPIO wake → boot → init BLE → read/increment NVS → AES-128-CCM → broadcast ~2 s →`sd_power_system_off()`.
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
- Payload: 4-byte monotonic counter, 1-byte command (0x01 = unlock, 0x02 = lock), 8-byte AES-CCM MIC.
- Generation and crypto via [ImmoCommon](https://github.com/LPFchan/ImmoCommon).
- Full protocol spec: [ImmoCommon README § BLE Protocol](https://github.com/LPFchan/ImmoCommon#ble-protocol).

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

