# Guillemot — Ninebot G30 BLE Immobilizer Receiver

Guillemot is the deck receiver in a three-part immobilizer system ([Uguisu](https://github.com/LPFchan/Immogen/tree/main/Uguisu) fob + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. It validates encrypted BLE advertisements from the fob and controls an SR latch that gates battery-to-ESC power via an inline XT60 splice. Male/female pigtails make it fully reversible—no permanent modification to the vehicle. Protocol and cryptography are handled by the shared [lib/](https://github.com/LPFchan/Immogen/tree/main/lib) in this monorepo ([Immogen](https://github.com/LPFchan/Immogen)).

---

## Hardware

### Placement

Mount Guillemot on the plastic underside of the deck for good 2.4 GHz RF.

### Components & BOM (~$20)


| Ref       | Part            | Notes                                                             | JLCPCB #         | Cost   |
| --------- | --------------- | ----------------------------------------------------------------- | ---------------- | ------ |
| U1        | XIAO nRF52840   | MCU: BLE scanning, latch control, buzzer PWM                      | Seeed            | $8.23  |
| U2        | SN74LVC2G02DCTR | SR latch: Q = unlock. 10 nF + 100 kΩ differentiator for POR pulse | C94600 (ext.)    | $0.53  |
| PS1       | TPSM365R6V3RDNR | Buck: 3–65 V in, 3.3 V/600 mA, 4 μA Iq. Feeds XIAO 3V3 directly.  | C18208843 (ext.) | $3.59  |
| Q1        | IPB042N10N3G    | Main MOSFET: 100 V/137 A, D2PAK, low-side GND return              | C69300 (ext.)    | $0.75  |
| Q2        | AO3422          | Pre-charge FET, inrush ~0.8 A. Driven by latch Q.                 | C37130 (ext.)    | $0.23  |
| Q3        | SI2309CDS       | Bleeder P-FET: disconnects 10 kΩ bleeder when locked (0 μA)       | C10493 (ext.)    | $0.23  |
| Q4        | 2N7002          | Gate drive                                                        | C8545            | $0.01  |
| Q5, Q6    | MMBT3904        | Buzzer driver                                                     | C20526           | $0.02  |
| BZ1       | FUET-1230       | Buzzer: 12×12×3 mm SMD, 4 kHz. Driven by MMBT3904                 | C391037 (ext.)   | $0.47  |
| D1        | BZT52C12-7-F    | 12 V Zener                                                        | C124196 (ext.)   | $0.11  |
| D2        | BZT52C10-7-F    | 10 V Zener                                                        | C155227 (ext.)   | $0.10  |
| D3, D4    | 1N4148W         | Gate isolation from buzzer rail sag                               | C81598           | $0.01  |
| D5        | SMBJ45A         | 45 V TVS diode for transient suppression                          | C114005 (ext.)   | $0.08  |
| C1        | 10 nF (0603)    | POR pulse differentiator                                          | C57112           | $0.003 |
| C2        | 100 µF (1210)   | Bulk decoupling. EMK325ABJ107MM-P 16V X5R ±20%                   | C90143 (ext.)    | $0.31  |
| C3, C4    | 1 µF (0603)     | RC delay bypass, gate isolation                                   | C15849           | $0.01  |
| R1, R2    | 100 kΩ (0603)   | Latch input pull-downs (prevent floating during WDT reset)        | C25803           | $0.006 |
| R3, R7    | 100 kΩ (0603)   | Gate drive, RC delay (τ = 100 ms)                                 | C25803           | $0.006 |
| R4        | 1 MΩ (0603)     | P-FET gate pull-up                                                | C22935           | $0.001 |
| R5        | 10 kΩ (0603)    | Bleeder resistor                                                  | C25804           | $0.001 |
| R6        | 4.7 kΩ (0603)   | Piezo buzzer discharge resistor                                   | C23162           | $0.001 |
| R8        | 47 Ω 2 W (2512) | Pre-charge resistor, inrush ~0.77–0.89 A                         | C136992 (ext.)   | $0.10  |
| TP1–5     | XT60 + 12 AWG   | Pigtails and GND jumper                                           | Generic          | —      |
| —         | PCB             | 54×34 mm, 2 oz outer                                              | JLCPCB           | $1.60  |


JLCPCB P&P for SMT. Hand-solder XIAO edges, XT60 pigtails, and the GND jumper.

### PCB Layout

4-layer, 54×34 mm, 2 oz outer copper, designed for 30 A continuous.

- **12 AWG GND jumper (critical):** Main battery return bypasses inner layers. Run an external silicone wire from the `BAT-` XT60 pad to the MOSFET source pad.
- **Power traces:** `+BATT` and `ESC-` as top-layer pours. No high-current vias.
- **Clearance:** ≥ 0.6 mm between 42 V power and 3.3 V logic.
- **Pre-charge trace:** Q2 → R8 → `ESC-` routed at **1.0 mm** to survive initial ~40 W inrush surge.
- **RF keepout:** Strip copper on all layers under the XIAO's BLE antenna trace.
- **Assembly:** Single-sided (F.Cu) SMT for easy JLCPCB PCBA.

### Operation

- **Unlock flow:** Uguisu advert → MCU validates → SET latch → Q HIGH → pre-charge 100 ms → MOSFET ON → buzzer tone.
- **Lock flow:** Uguisu advert → MCU validates → buzzer tone → RESET latch → Q LOW → P-FET OFF, pre-charge OFF → MOSFET OFF.
- **Power (locked):** ~306 μA (buck Iq + 5% duty-cycled scan + latch). At 306 μA, a G30 (15.3 Ah) would theoretically take ~5.7 years to drain; battery self-discharge (~12 months) dominates in practice. See `logs/15-BLE_POWER_ANALYSIS_AND_DOCUMENTATION_DISCREPANCY.md` for derivation.
- **Buzzer timing:** Unlock tone ~34 ms after 10 V rail up; lock tone before RESET.

### Design Notes

- **EasyEDA → KiCad:** Schematics and PCBs are designed in KiCad. [easyeda2kicad](https://github.com/wokwi/easyeda2kicad) is used to import symbols and footprints from EasyEDA/LCSC. Example: `easyeda2kicad --full --lcsc_id=Cxxxx` (replace `Cxxxx` with the LCSC part number).
- **Latch Input Pull-downs (100 kΩ):** R1 and R2 hold `MCU_D0` (SET) and `MCU_D1` (RESET) strictly LOW. This is critical because the nRF52840's pins float (high-impedance) during the 8-second hardware Watchdog Timer (WDT) reset cycle. Without these resistors, noise or capacitive coupling could randomly toggle the latch and power state while the MCU reboots.
- **AO3422:** 55 V Vds provides margin over the 42 V bus when locked.
- **Gate drive R (100 kΩ):** Protects the 2N7002 and BZT Zeners.
- **POR circuit (10 nF + 100 kΩ):** 1 ms RC differentiator—brief pulse zeros the latch at power-on.
- **RC delay (100 kΩ + 1 μF):** 1N4148W bypass for fast turn-off.
- **Gate isolation (1N4148W + cap):** Buzzer draw exceeds bleeder supply; diode and 1 μF hold cap prevent gate sag during tones (Vgs ≈ 9.3 V).
- **Gate pull-down (MMBT3904):** Via Q̄/RC, discharges hold cap for instant OFF.
- **IPB042N10N3G:** Outperforms TO-220 standing upright in sealed deck enclosures when thermal pad is stitched to B.Cu. **IPB035N10NF2S (3.5 mΩ)** is the long-term successor depending on LCSC stock.

---

## Software

### Firmware

[PlatformIO](https://platformio.org/) project in `firmware/`

Scanning is duty-cycled (25 ms every 500 ms, 5% duty) for reliable detection. P(detect) = 100% per button press because `scanWindow (25 ms) ≥ advInterval (20 ms)` guarantees full phase coverage. The receiver rejects any payload with an invalid AES-CCM MIC or a counter less than or equal to the last seen value (anti-replay). The receiver is also protected against MCU hangs by an 8-second hardware Watchdog Timer (WDT) which triggers a clean reboot without disrupting the latch state.

### Protocol

Validation and crypto via shared [lib/](https://github.com/LPFchan/Immogen/tree/main/lib). See [Immogen README § BLE Protocol](https://github.com/LPFchan/Immogen#ble-protocol) for full specification.

### Onboarding

Use [Whimbrel](https://github.com/LPFchan/Whimbrel) for both tasks:

- **Firmware flashing:** Web-based flasher via Web Serial. Enter bootloader mode with a double-tap on reset.
- **Key provisioning:** Connect via USB; Whimbrel injects the shared AES-128 key. Physical presence is required. The firmware has no serial commands to read the key back.

---

## Safety & Notes

- Prototype security and power-interrupt device. Use at your own risk.
- Do not test lock behavior while riding.
- Not affiliated with Segway-Ninebot.
- Avoid committing KiCad per-user state (`*.kicad_prl`) or lock files.

