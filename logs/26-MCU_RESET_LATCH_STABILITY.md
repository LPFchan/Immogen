# MCU Reset and Latch Stability Analysis

*Date: 2026-03-10 14:31*

## Overview
A technical critique was evaluated regarding the stability of the hardware SR latch during nRF52840 reset cycles (Watchdog Timer, brownout, or pin reset). The core concern was that MCU GPIOs revert to a high-impedance (floating) state during reset, potentially causing the latch to glitch and toggle the scooter's power state due to noise or capacitive coupling.

## Technical Analysis
- **MCU Behavior:** During any reset event, the nRF52840 disconnects internal pull resistors and tri-states its GPIOs. This state persists until the bootloader and firmware re-initialize the pins (a window of several milliseconds to seconds, especially during a WDT timeout).
- **Latch Sensitivity:** The `SN74LVC2G02` (NOR-gate SR Latch) uses high-impedance CMOS inputs. Without external biasing, these inputs are susceptible to ambient EMI or crosstalk. If a floating input drifts above the logic threshold, it could trigger a `SET` or `RESET` pulse, unintentionally changing the scooter's power state.
- **Safety Impact:** Since a Watchdog Timer (WDT) was recently implemented to handle firmware hangs, the stability of the hardware latch during the subsequent reboot is critical to ensure the scooter does not lose power mid-ride.

## Hardware Verification
Analysis of the `Guillemot` KiCad schematics and PCB layout confirmed that the design is already robust against this failure mode:
- **Resistor R1 (100 kΩ):** Tied between `MCU_D0` (SET) and `GND`.
- **Resistor R2 (100 kΩ):** Tied between `MCU_D1` (RESET) and `GND`.
- **Function:** These external pull-down resistors ensure that both latch inputs are held strictly `LOW` the moment the MCU enters reset, preventing any glitching until the firmware regains control.

## Documentation Improvements
While the hardware was correctly designed, the technical documentation in `Guillemot/README.md` was found to be inaccurate:
1. **BOM Correction:** R1 and R2 were previously grouped with pull-up resistors. The BOM has been updated to explicitly identify them as **Latch input pull-downs**.
2. **Design Note Addition:** A new entry was added to the "Design Notes" section to document the safety purpose of these resistors, specifically their role in maintaining latch stability during the 8-second WDT reset cycle.

## Conclusion
The hardware design correctly anticipates the high-impedance reset state of the nRF52840. The firmware-based WDT can be safely used without risk to the hardware state retention. The project documentation now accurately reflects this critical safety feature.
