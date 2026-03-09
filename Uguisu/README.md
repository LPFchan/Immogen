# Uguisu — Ninebot G30 BLE Immobilizer Fob

Uguisu is the BLE key fob in a three-part immobilizer system ([Guillemot](https://github.com/LPFchan/Immogen/tree/main/Guillemot) receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. It broadcasts short, encrypted BLE advertisements on button press to unlock or lock the vehicle. This repository contains the fob firmware and hardware design files. Protocol and cryptography via the shared [lib/](https://github.com/LPFchan/Immogen/tree/main/lib) in this monorepo ([Immogen](https://github.com/LPFchan/Immogen)).

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

KiCad (`hardware/Uguisu.kicad_sch` / `hardware/Uguisu.kicad_pcb`).

### GPIO


| Function      | Pin | nRF P0.x | Notes                                              |
| ------------- | --- | -------- | -------------------------------------------------- |
| Button (KEY1) | D0  | P0.02    | Active-low, internal pull-up, wake-from-system-off |
| Blue (LED1)   | D1  | P0.03    | LED1 pin 1, R1 120 Ω, active-low (sink)            |
| Red (LED1)    | D2  | P0.28    | LED1 pin 2, R2 330 Ω, active-low (sink)            |
| Green (LED1)  | D3  | P0.29    | LED1 pin 3, R3 120 Ω, active-low (sink)            |


### RGB LED Behaviour

| Situation              | Color  | Pattern                          |
| ---------------------- | ------ | -------------------------------- |
| Unlock                 | Green  | Single pulse (fade-in/hold/fade-out) |
| Lock                   | Red    | Single pulse (fade-in/hold/fade-out) |
| Serial provisioning    | Blue   | Repeating pulse loop             |
| Low battery + Unlock   | Green  | Rapid pulse loop for 2 s         |
| Low battery + Lock     | Red    | Rapid pulse loop for 2 s         |
| Fatal error (FS fail)  | Red    | Fast blink + onboard LED         |

All timing constants live in `firmware/include/led_effects.h`. Use `tools/led_visualizer.html` (repo root; open in any browser) to preview and fine-tune durations before flashing.

### Operation

- **Unlock flow:** Short press (< 1 s) → green flash → BLE broadcast ~2 s → sleep.
- **Lock flow:** Long press (≥ 1 s) → red flash → BLE broadcast ~2 s → sleep.
- **Low battery:** Battery checked after button press; replaces single flash with 2 s pulse loop in the same color while advertising.
- **Power:** ~30 μA standby (nRF52840 system-off + XIAO charging IC), ~0.006 mAh per press (12–18 months between charges on 400 mAh LiPo). Charging via USB-C.

### Design Notes

- **EasyEDA → KiCad:** Schematics and PCBs are designed in KiCad. [easyeda2kicad](https://github.com/wokwi/easyeda2kicad) is used to import symbols and footprints from EasyEDA/LCSC. Example: `easyeda2kicad --full --lcsc_id=Cxxxx` (replace `Cxxxx` with the LCSC part number).
- **NVS wear:** Counter written every press (System OFF kills RAM). At 10 unlocks/day, ~2.7 years; wear-leveling extends this.

---

## Software

### Firmware

[PlatformIO](https://platformio.org/) project in `firmware/`.

**Boot flow:**

```
led::init()
g_store.begin()          — fail → error_loop() [red rapid blink + onboard LED, forever]
load_provisioning()

VBUS present?
  yes → prov_led_task (blue pulse, FreeRTOS)
        ensure_provisioned()   [waits for PROV: serial packet]
        stop prov_led_task
        led::off()

Bluefruit.begin() / setName / setTxPower

wait_for_button_press_release(10 s)
  timeout → system_off()

command = press ≥ 1 s ? Lock : Unlock
cmd_pin = Lock ? PIN_LED_R : PIN_LED_G
low_bat = readVbat_mv() < LED_LOWBAT_MV_THRESHOLD

[crypto: nonce → msg → AES-128-CCM MIC]
  fail → system_off()

g_store.update(counter)
start_advertising_once()     — BLE advertising starts
adv_start = millis()

low_bat?
  yes → flash_low_battery(cmd_pin)   [~2 s repeating pulses, same color as command]
  no  → flash_once(cmd_pin, ...)     [~750 ms single pulse]

delay(UGUISU_ADVERTISE_MS - elapsed)   — wait out rest of advertising window

system_off()
```

Next press wakes via GPIO (hardware SENSE on `UGUISU_PIN_BUTTON_NRF`).

### Protocol

Generation and crypto via shared [lib/](https://github.com/LPFchan/Immogen/tree/main/lib). See [Immogen README § BLE Protocol](https://github.com/LPFchan/Immogen#ble-protocol) for full specification.

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

