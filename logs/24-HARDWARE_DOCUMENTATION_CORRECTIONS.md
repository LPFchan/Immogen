# Guillemot Hardware Documentation Corrections

*Date: 2026-03-10 09:57*

This document outlines corrections to the hardware documentation, specifically targeting errors in Section 6 of the original technical documentation.

## Section 6: Load Switching and Protection (Corrected)

The Guillemot PCB employs a low-side load switch topology to interrupt the ESC ground return path, effectively disabling the scooter when locked. A 10V gate drive is derived directly from the battery voltage to fully enhance the switching MOSFET.

### 6.1 Topology and 10V Gate Drive

- **Q1 (IPB042N10N3G) — LOW-SIDE SWITCH:**
  - **Drain** ← ESC- (load return path)
  - **Source** → GND (battery negative)
  - **Gate** ← ISO_GATE (10V when unlocked, 0V when locked)
  - **Function:** Interrupts the ground connection of the ESC. Since it switches the negative side of a 42V load, it operates as a low-side switch.

- **Q3 (SI2309CDS) — 10V GATE DRIVE SWITCH:**
  - **Source** ← +BATT (42V Nominal)
  - **Drain** → R5 + D2 (10V Zener) → 10V_RAIL → Q1 Gate
  - **Gate** ← D1 (12V Zener clamp) ← Q4 (2N7002) ← Q_UNLOCK signal
  - **Function:** Switches the main battery voltage through a Zener regulator to create a 10V gate drive for Q1. This design achieves zero quiescent current when the system is locked (Q3 is fully off, breaking the current path).

### 6.2 Gate Protection

- **D1 (BZT52C12-7-F) — GATE PROTECTION:**
  - **Location:** Across the Gate-Source of Q3.
  - **Function:** Clamps the Gate-to-Source voltage (V_GS) of the P-FET Q3 to a maximum of -12V. This is critical for survival because without it, turning on Q4 would pull Q3's gate to ground, creating a V_GS of -42V. This would vastly exceed the SI2309CDS's absolute maximum rating of -20V and destroy the transistor. D1 ensures safe operation of the gate drive circuitry.