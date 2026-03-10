# Master Document Consolidation & Verification

*Date: 2026-03-10 01:28*

**Status:** COMPLETE — Master document deleted, all content verified in monorepo.

---

## Context

A standalone master document (`BLE_Immobilizer_G30_Detailed(10).md`) existed at the project root alongside the Immogen monorepo. This document contained system-level design specifications for the three-part immobilizer (Uguisu fob, Guillemot receiver, Whimbrel web app). To reduce maintenance burden and prevent documentation drift, a consolidation audit was performed to verify all content existed in the monorepo before deletion.

---

## Audit Scope

The master document covered eight major sections:

1. **Overview** — System architecture, deck placement, signal diagram
2. **BLE Protocol** — Advertisement-based messaging, 9-byte payload spec
3. **Uguisu (Key Fob)** — Hardware BOM (~$10), firmware boot flow, power budget
4. **Guillemot (Deck Receiver)** — Hardware specs, PCB layout, signal flow, BOM (~$25–30)
5. **Whimbrel (Provisioning App)** — Web Serial protocol, security model, key generation
6. **Build Notes** — Assembly, conformal coating, buzzer timing
7. **Design Notes** — Component rationale (AO3422, POR circuit, gate isolation, MOSFET selection, NVS wear)
8. **TBD** — Pending items (enclosure CAD, validation)

---

## Coverage Analysis

### Section-by-Section Mapping

| Master Section | Repo Location | Status | Notes |
|---|---|---|---|
| **Overview** | `README.md` § Overview | ✅ COMPLETE | Identical ASCII diagram and system description |
| **BLE Protocol** | `README.md` § BLE Protocol | ✅ COMPLETE | Updated with company ID offset spec |
| **Uguisu BOM** | `Uguisu/README.md` § Hardware | ✅ COMPLETE | Upgraded to 400 mAh LiPo + new switch/LED |
| **Uguisu firmware** | `Uguisu/README.md` § Software | ✅ COMPLETE | Detailed boot flow pseudocode (lines 76–109) |
| **Guillemot hardware/BOM** | `Guillemot/README.md` § Hardware | ✅ COMPLETE | Comprehensive BOM with all passives |
| **PCB layout specs** | `Guillemot/README.md` § PCB Layout | ✅ COMPLETE | All 6 critical specs present (GND jumper, clearance, RF keepout, etc.) |
| **Signal flow** | `Guillemot/README.md` § Operation | ✅ COMPLETE | Unlock/lock/power flows documented |
| **Design notes (7 items)** | `Guillemot/README.md` § Design Notes | ✅ COMPLETE | AO3422 rationale, POR circuit, gate isolation, MOSFET selection, all present |
| **NVS wear** | `Uguisu/README.md` § Design Notes | ✅ COMPLETE | 2.7 years / wear-leveling documented |
| **EasyEDA→KiCad** | Both READMEs | ✅ COMPLETE | Tool reference and usage present |
| **Whimbrel security** | `Guillemot/README.md` § Onboarding + `Uguisu/README.md` § Onboarding | ✅ COMPLETE | Write-only security, physical presence requirement documented |
| **Whimbrel ephemeral keys** | `Whimbrel/README.md` § Ephemeral Memory | ✅ COMPLETE | More detailed than master (includes device-replacement scenario) |
| **TBD items** | `README.md` § TBD | ✅ COMPLETE | Enclosure CAD + validation identical |

### Discrepancies & Corrections

The master document contained **outdated values** that have been corrected in the repo based on real measurement and analysis:

| Parameter | Master Doc | Repo Docs | Source |
|---|---|---|---|
| **Guillemot locked current** | ~22 μA | ~306 μA | `logs/15-BLE_POWER_ANALYSIS_AND_DOCUMENTATION_DISCREPANCY.md` |
| **Uguisu standby current** | < 5 μA | ~30 μA | Includes XIAO charging IC |
| **Uguisu per-press energy** | ~0.004 mAh | ~0.006 mAh | Real measurement via PPK2 |
| **Uguisu battery capacity** | 100 mAh | 400 mAh | Hardware upgrade (TW-502535 vs. generic) |
| **Guillemot scan duty cycle** | 20 ms / 2 s (1%) | 25 ms / 500 ms (5%) | Updated for better detection (see logs/8, logs/11) |

These corrections reflect iterative refinement through:
- Power analysis (logs/15)
- Architecture review & critique (logs/7)
- BLE timing simulation (logs/10)
- Build validation and real-world testing

### Minor Items Not in Repo

Three items appeared exclusively in the master document:

1. **Conformal coating instructions** — "Conformal coat PCB (exclude USB-C, XT60 joints, and buzzer port). Drill 3 mm acoustic port + mesh in scooter battery box for BZ1."
   - **Status:** Discarded. User opted not to conformal coat final build.

2. **QSPI flash sleep (0xB9)** — Firmware detail: `nrf_qspi_mem_object_read(0, sizeof(...))`; `nrf_qspi_mem_deep_power_down()`; `sd_power_system_off()`.
   - **Status:** Discarded. Not utilized in firmware; firmware code is authoritative.

3. **Whimbrel ephemeral session keys** — "Closing the tab permanently deletes the session key."
   - **Status:** Already in `Whimbrel/README.md` § Ephemeral Memory (lines 30–32), more comprehensive.

---

## Conclusion

✅ **All actionable content from the master document is present in the monorepo.**

The distributed documentation is **more accurate and comprehensive** than the master document:
- **Immogen/README.md** — System overview, repository structure, protocol spec
- **Immogen/Guillemot/README.md** — Receiver design, PCB layout, operation, design rationale
- **Immogen/Uguisu/README.md** — Fob design, GPIO mapping, LED behavior, boot flow
- **Whimbrel/README.md** — Provisioning protocol, security model, ephemeral key storage
- **Immogen/logs/** — 15+ technical deep-dives, architecture reviews, migration guides, power analysis

**Master document deleted:** 2026-03-10.

---

## Recommended Reading Order

For new contributors:

1. `Immogen/README.md` — Start here for system overview
2. `Immogen/Guillemot/README.md` — Receiver hardware & firmware
3. `Immogen/Uguisu/README.md` — Fob hardware & firmware
4. `Whimbrel/README.md` — Provisioning workflow
5. `Immogen/logs/2-CODEBASE_TECHNICAL_WRITEUP.md` — Deep technical reference
6. `Immogen/logs/15-BLE_POWER_ANALYSIS_AND_DOCUMENTATION_DISCREPANCY.md` — Power budget derivation
