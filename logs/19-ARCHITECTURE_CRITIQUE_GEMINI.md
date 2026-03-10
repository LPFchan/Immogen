# Immogen Architecture Critique & Restructuring Plan

*Author: Gemini*
*Date: 2026-03-10 08:56*

This document outlines critical inefficiencies, architectural flaws, and documentation errors in the current Immogen BLE immobilizer system. It focuses exclusively on areas requiring immediate restructuring or correction, assuming a ground-up redesign approach.

## 1. Uguisu (Key Fob) Inefficiencies

The fundamental flaw in the fob's firmware is its state management during the wake-from-sleep cycle.

*   **The LittleFS Power Drain:** The most glaring inefficiency is in `immo_storage::load()`. On every single button press, the fob wakes from `system_off` deep sleep and linearly scans an up-to-4KB LittleFS log file (requiring up to 512 sequential `read()` and `crc32_ieee()` calls) simply to find the last anti-replay counter value. This wastes precious milliseconds of active CPU time and flash memory power draw before the BLE radio even turns on.
*   **Crypto Software Fallback:** `immo_crypto.cpp` utilizes the `sd_ecb_block_encrypt()` SoftDevice call to manually implement AES-CCM in software via ECB looping. While functional, the nRF52840 possesses a dedicated hardware `NRF_CCM` peripheral. Software looping increases active CPU time.

**Restructuring Solution:**
1.  **Zero-Cost Retention:** Utilize the nRF52's General Purpose Retention Registers (`GPREGRET` and `GPREGRET2`) or RAM sections explicitly marked `__attribute__((section(".noinit")))`. These survive `sd_power_system_off()`. On cold boot (battery insertion), scan LittleFS once and load the counter into retention RAM. On subsequent presses, read from retention RAM in microseconds, increment, transmit, write the new value to LittleFS, update retention RAM, and sleep.
2.  **Flash Optimization:** Replace the LittleFS dependency for the counter log with Nordic's Flash Data Storage (FDS) module or a custom raw circular buffer inside a single 4KB flash page. This eliminates wear-leveling filesystem overhead for an 8-byte record and turns a 512-iteration loop into a fast binary search or single-pointer read.

## 2. Guillemot (Receiver) Inefficiencies

The receiver firmware architecture improperly couples real-time BLE events with slow, blocking hardware actuation.

*   **Blocking the BLE Stack:** In `main.cpp`, `handle_valid_command()` plays a two-tone buzzer using a blocking `delay()` via `buzzer_tone_ms()`. Crucially, this is executed synchronously within the Bluefruit `scan_callback` (which runs in a high-priority FreeRTOS task). This stalls the entire BLE scanner for ~260ms per valid command. During this window, any other BLE packets (e.g., from rapid button presses or multiple fobs) will be dropped.
*   **Continuous 5% Duty Cycle:** The 25ms scan window every 500ms draws ~250µA average. While acceptable for a large 15Ah scooter battery, it represents a constant vampire drain.

**Restructuring Solution:**
1.  **Event-Driven Tasking:** Decouple cryptographic verification and hardware actuation from the BLE radio callback. The `scan_callback` should only validate the Company ID and push raw payloads into a FreeRTOS queue (`xQueueSendFromISR`).
2.  **Non-Blocking Actuation:** A dedicated `ImmoTask` consumes the queue, verifies the MIC, updates the counter, sets the hardware latch, and triggers a non-blocking software timer (`xTimerCreate`) or utilizes hardware PWM for the buzzer tones.

## 3. Hardware Documentation Disconnect

The `18-CODEBASE_TECHNICAL_DOCUMENTATION.md` contains fundamental misunderstandings of the physical KiCad design, misrepresenting the Guillemot PCB architecture:

1.  **The Main MOSFET Topology:** The documentation claims `Q1` (IPB042N10N3G) is a high-side switch (`Drain <- +BATT, Source -> ESC-`). The actual schematic netlist and component selection prove it is a **low-side switch** on the GND return path. If it were configured as a high-side switch, driving it with the implemented logic-level signal circuitry would instantly destroy it.
2.  **The 10V Gate Drive:** The documentation completely misses the elegance of the actual gate drive design. The SR Latch `Q_UNLOCK` drives an N-FET (`Q4`), which pulls down a P-FET (`Q3`). This switches the full 42V battery into a 10V Zener-clamped linear regulator (`R5` + `D2`). This creates a powerful 10V rail (`10V_RAIL`) to fully saturate the massive Q1 gate *only* when unlocked, resulting in zero quiescent power draw when locked. The documentation incorrectly states the 10V rail is strictly for the buzzer.
3.  **P-FET Gate Protection:** The documentation implies a 42V gate-to-source voltage is present on `Q3`. This would instantly exceed its 20V absolute maximum oxide rating, destroying the component. In reality, the schematic includes a `BZT52C12-7-F` (D1) Zener diode specifically placed to safely clamp Vgs to 12V.

**Restructuring Solution:**
*   Rewrite Section 6 (`Hardware Design — Guillemot PCB`) of the technical documentation to accurately reflect the low-side switching topology, the 10V switched gate-drive architecture, and the Vgs clamping protections. This is critical to ensure future maintainers do not make hazardous design modifications based on flawed documentation.