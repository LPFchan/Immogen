# Guillemot — Ninebot G30 BLE Immobilizer Receiver

Guillemot is the deck receiver in a three-part immobilizer system ([Uguisu](https://github.com/LPFchan/Uguisu) fob + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. It validates encrypted BLE advertisements from the fob and controls an SR latch that gates battery-to-ESC power via an inline XT60 splice. Male/female pigtails make it fully reversible—no permanent modification to the vehicle. Protocol and cryptography are handled by [ImmoCommon](https://github.com/LPFchan/ImmoCommon).

---

## Hardware

### Placement

Mount Guillemot on the plastic underside of the deck for good 2.4 GHz RF.

### Components & BOM (~$25–30)


| Ref   | Part            | Notes                                                             | LCSC         | Cost   |
| ----- | --------------- | ----------------------------------------------------------------- | ------------ | ------ |
| U1    | XIAO nRF52840   | MCU: BLE scanning, latch control, buzzer PWM                      | Seeed/Amazon | $6.00  |
| PS1   | TPSM365R6V3RDNR | Buck: 3–65 V in, 3.3 V/600 mA, 4 μA Iq. Feeds XIAO 3V3 directly.  | C18208843    | $4.30  |
| Q1    | IPB042N10N3G    | Main MOSFET: 100 V/137 A, D2PAK, low-side GND return              | C69300       | $1.36  |
| U2    | SN74LVC2G02DCTR | SR latch: Q = unlock. 10 nF + 100 kΩ differentiator for POR pulse | C94600       | $0.50  |
| Q2    | AO3422          | Pre-charge FET, inrush ~0.8 A. Driven by latch Q.                 | C37130       | —      |
| R8    | 47 Ω 2 W        | Pre-charge resistor                                               | C136992      | $0.03  |
| Q4    | 2N7002          | Gate drive                                                        | C8545        | —      |
| —     | 100 kΩ + 1 μF   | RC delay τ = 100 ms. 1N4148W bypass for fast turn-off             | JLCPCB Basic | —      |
| —     | 1N4148W + 1 μF  | Gate isolation from buzzer rail sag                               | JLCPCB Basic | —      |
| BZ1   | FUET-1230       | Buzzer: 12×12×3 mm SMD, 4 kHz. Driven by MMBT3904                 | C391037      | $0.22  |
| Q3    | SI2309CDS       | Bleeder P-FET: disconnects 10 kΩ bleeder when locked (0 μA)       | C10493       | $0.10  |
| TP1–5 | XT60 + 12 AWG   | Pigtails and GND jumper                                           | Generic      | —      |
| —     | PCB 5×          | 54×34 mm, 2 oz outer                                              | JLCPCB       | $10–15 |


JLCPCB P&P for SMT. Hand-solder XIAO edges, XT60 pigtails, and the GND jumper.

### PCB Layout

4-layer, 54×34 mm, 2 oz outer copper, designed for 30 A continuous.

- **12 AWG GND jumper (critical):** Main battery return bypasses inner layers. Run an external silicone wire from the `BAT-` XT60 pad to the MOSFET source pad.
- **Power traces:** `+BATT` and `ESC-` as top-layer pours. No high-current vias.
- **Clearance:** ≥ 0.6 mm between 42 V power and 3.3 V logic.
- **RF keepout:** Strip copper on all layers under the XIAO's BLE antenna trace.

### Operation

- **Unlock flow:** Uguisu advert → MCU validates → SET latch → Q HIGH → pre-charge 100 ms → MOSFET ON → buzzer tone.
- **Lock flow:** Uguisu advert → MCU validates → buzzer tone → RESET latch → Q LOW → P-FET OFF, pre-charge OFF → MOSFET OFF.
- **Power (locked):** ~22 μA (buck Iq + duty-cycled scan + latch). At 22 μA, a G30 (15.3 Ah) would theoretically take ~79 years to drain; battery self-discharge dominates in practice.
- **Buzzer timing:** Unlock tone ~34 ms after 10 V rail up; lock tone before RESET.

### Design Notes

- **AO3422:** 55 V Vds provides margin over the 42 V bus when locked.
- **Gate drive R (100 kΩ):** Protects the 2N7002 and BZT Zeners.
- **POR circuit (10 nF + 100 kΩ):** 1 ms RC differentiator—brief pulse zeros the latch at power-on.
- **Gate isolation (1N4148W + cap):** Buzzer draw exceeds bleeder supply; diode and cap prevent gate sag during tones.
- **IPB042N10N3G:** Thermal pad should be stitched to B.Cu for sealed deck enclosures.

---

## Software

### Firmware

[PlatformIO](https://platformio.org/) project in `firmware/guillemot/`

Scanning is duty-cycled (20 ms every 2 seconds) to conserve power. The receiver rejects any payload with an invalid AES-CCM MIC or a counter less than or equal to the last seen value (anti-replay).

### Protocol

- Advertisement-based BLE (no persistent connection).
- Payload: 4-byte monotonic counter, 1-byte command (0x01 = unlock, 0x02 = lock), 4-byte AES-CCM MIC.
- Validation and crypto via [ImmoCommon](https://github.com/LPFchan/ImmoCommon).
- Full protocol spec: [ImmoCommon README § BLE Protocol](https://github.com/LPFchan/ImmoCommon#ble-protocol).

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

