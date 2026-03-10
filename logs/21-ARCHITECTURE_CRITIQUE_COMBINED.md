# Immogen Architecture Critique & Restructuring Plan (Combined)

*Authors: Gemini & Opus*
*Date: 2026-03-10 09:06*

This document consolidates critical inefficiencies, architectural flaws, security vulnerabilities, and hardware documentation errors identified by both Gemini and Opus in the current Immogen BLE immobilizer system.

## 1. Uguisu (Key Fob) Firmware & Inefficiencies

### The LittleFS Power Drain (Gemini)

The fundamental flaw in the fob's firmware is its state management during the wake-from-sleep cycle. The most glaring inefficiency is in `immo_storage::load()`. On every single button press, the fob wakes from `system_off` deep sleep and linearly scans an up-to-4KB LittleFS log file (requiring up to 512 sequential `read()` and `crc32_ieee()` calls) simply to find the last anti-replay counter value. This wastes precious milliseconds of active CPU time and flash memory power draw before the BLE radio even turns on.

**Restructuring Solution:**

1. **Zero-Cost Retention:** Utilize the nRF52's General Purpose Retention Registers (`GPREGRET` and `GPREGRET2`) or RAM sections explicitly marked `__attribute__((section(".noinit")))`. These survive `sd_power_system_off()`. On cold boot (battery insertion), scan LittleFS once and load the counter into retention RAM. On subsequent presses, read from retention RAM in microseconds, increment, transmit, write the new value to LittleFS, update retention RAM, and sleep.
2. **Flash Optimization:** Replace the LittleFS dependency for the counter log with Nordic's Flash Data Storage (FDS) module or a custom raw circular buffer inside a single 4KB flash page. This eliminates wear-leveling filesystem overhead for an 8-byte record and turns a 512-iteration loop into a fast binary search or single-pointer read.

### Button press timing offset (Opus)

`wait_for_button_press_release()` runs after boot initialization (~100-200 ms). The button press that woke the device from system-off is already in progress. Measured press duration is shorter than actual by the boot time. The 1000 ms long-press threshold for Lock actually requires ~1200 ms of physical holding. Either compensate for boot time or document the effective threshold.

### Button timeout is excessive (Opus)

10 seconds awake at ~5 mA waiting for a button press that already triggered the wake. 2-3 seconds is sufficient. Saves ~35-40 mJ per accidental wake.

### FreeRTOS task for provisioning LED is overkill (Opus)

Uguisu spawns a FreeRTOS task with 512 bytes of stack just to blink a blue LED. A timer interrupt or non-blocking `millis()`-based approach uses zero stack and is simpler.

## 2. Guillemot (Receiver) Firmware & Inefficiencies

### Blocking the BLE Stack (Gemini)

In `main.cpp`, `handle_valid_command()` plays a two-tone buzzer using a blocking `delay()` via `buzzer_tone_ms()`. Crucially, this is executed synchronously within the Bluefruit `scan_callback` (which runs in a high-priority FreeRTOS task). This stalls the entire BLE scanner for ~260ms per valid command. During this window, any other BLE packets (e.g., from rapid button presses or multiple fobs) will be dropped.

### Continuous 5% Duty Cycle (Gemini)

The 25ms scan window every 500ms draws ~250µA average. While acceptable for a large 15Ah scooter battery, it represents a constant vampire drain.

**Restructuring Solution (Gemini):**

1. **Event-Driven Tasking:** Decouple cryptographic verification and hardware actuation from the BLE radio callback. The `scan_callback` should only validate the Company ID and push raw payloads into a FreeRTOS queue (`xQueueSendFromISR`).
2. **Non-Blocking Actuation:** A dedicated `ImmoTask` consumes the queue, verifies the MIC, updates the counter, sets the hardware latch, and triggers a non-blocking software timer (`xTimerCreate`) or utilizes hardware PWM for the buzzer tones.

### No watchdog on Guillemot (Opus)

Guillemot is an always-on, safety-critical receiver. If the firmware hangs (SoftDevice error, flash corruption, radio lockup), the scooter stays in whatever state it was in — potentially unlocked indefinitely. `sd_app_evt_wait()` in `loop()` sleeps forever if events stop arriving. The nRF52840 WDT should be configured; the SoftDevice supports WDT coexistence.

## 3. Cryptography & Security

### `verify_payload()` may not check `ccm_mic_8()` return value (Opus)

`ccm_mic_8()` returns `false` when `sd_ecb_block_encrypt()` fails (SoftDevice busy during a BLE event). If `verify_payload()` doesn't check this, the MIC comparison runs against an uninitialized or stale buffer, which could randomly match a received MIC. This is a potential authentication bypass.
**Fix:** Check the return value; reject the payload immediately on failure.

### Nonce wastes 9 bytes that should hold a session diversifier (Opus)

The nonce is `LE32(counter) || 0x00 * 9`. If the same key is ever re-provisioned with a counter reset (bug, manual intervention, flash corruption), nonce reuse breaks CCM entirely. A random session ID generated during provisioning and stored alongside the key would close this.

### Payload is authenticated but not encrypted (Opus)

Counter and command are sent in plaintext. Any passive BLE sniffer sees when you lock/unlock, which command was sent, and your counter value (reveals usage frequency). Full CCM encryption is free — the implementation already does the counter-mode work for the tag but doesn't apply the keystream to the plaintext.

### Manual CCM instead of hardware / Software Fallback (Gemini & Opus)

The code manually constructs B0 blocks and does CBC-MAC using `sd_ecb_block_encrypt()`. `immo_crypto.cpp` utilizes this SoftDevice call to manually implement AES-CCM in software via ECB looping. While functional, the nRF52840 possesses a dedicated hardware `NRF_CCM` peripheral. The SoftDevice restricts direct access, but its own CCM API may offer a higher-level path. Software looping increases active CPU time, and fewer lines of manual crypto = fewer places to get it wrong.

## 4. Anti-Replay & Storage

### Counter log rotation has a power-failure vulnerability (Opus)

`rotateIfNeeded_()` does: delete old log → rename current → old. If power is lost after the rename but before `update()` writes to the new current log:

1. On next boot, `load()` reads the current log — file doesn't exist — `last_counter_` = 0
2. `load()` does **not** read the old log file
3. Guillemot now accepts any counter > 0, including replays of previously-accepted values

**Fix:** `load()` should scan both current and old log files, taking the maximum counter from both.

## 5. BLE Protocol

### No delivery confirmation (Opus)

The fob broadcasts for 600 ms and sleeps with no acknowledgment. If out of range or if interference blocks the signal, the user gets zero feedback at the fob. The buzzer confirmation is only at the receiver end. For a security-critical device, a missed command is indistinguishable from a successful one at the point of use.

### Company ID 0xFFFF causes unnecessary processing (Opus)

0xFFFF is the Bluetooth SIG test/unregistered ID. Any other device using it triggers `parse_payload_from_report()` and a full MIC check before rejection. A registered company ID or a magic byte prefix in the MSD payload would filter earlier.

## 6. Hardware & Documentation

### Hardware Documentation Disconnect (Gemini)

The `18-CODEBASE_TECHNICAL_DOCUMENTATION.md` contains fundamental misunderstandings of the physical KiCad design, misrepresenting the Guillemot PCB architecture:

1. **The Main MOSFET Topology:** The documentation claims `Q1` (IPB042N10N3G) is a high-side switch (`Drain <- +BATT, Source -> ESC-`). The actual schematic netlist and component selection prove it is a **low-side switch** on the GND return path. If it were configured as a high-side switch, driving it with the implemented logic-level signal circuitry would instantly destroy it.
2. **The 10V Gate Drive:** The documentation completely misses the elegance of the actual gate drive design. The SR Latch `Q_UNLOCK` drives an N-FET (`Q4`), which pulls down a P-FET (`Q3`). This switches the full 42V battery into a 10V Zener-clamped linear regulator (`R5` + `D2`). This creates a powerful 10V rail (`10V_RAIL`) to fully saturate the massive Q1 gate *only* when unlocked, resulting in zero quiescent power draw when locked. The documentation incorrectly states the 10V rail is strictly for the buzzer.
3. **P-FET Gate Protection:** The documentation implies a 42V gate-to-source voltage is present on `Q3`. This would instantly exceed its 20V absolute maximum oxide rating, destroying the component. In reality, the schematic includes a `BZT52C12-7-F` (D1) Zener diode specifically placed to safely clamp Vgs to 12V.

**Restructuring Solution:**

- Rewrite Section 6 (`Hardware Design — Guillemot PCB`) of the technical documentation to accurately reflect the low-side switching topology, the 10V switched gate-drive architecture, and the Vgs clamping protections. This is critical to ensure future maintainers do not make hazardous design modifications based on flawed documentation.

### Hardware Design Issues — Guillemot (Opus)

- **No reverse polarity protection:** If the XT60 battery pigtail is soldered backwards, the N-FET body diode conducts and dumps reverse voltage into the buck converter and MCU. A series P-FET ideal diode or Schottky on the input would prevent this. Hand-soldered pigtails in a scooter environment make polarity reversal a real risk.
- **TVS clamping voltage may exceed downstream ratings:** SMBJ45A clamps at 72.7 V. The TPSM365R6V3RDNR has a 65 V abs max input. The 12 V Zener (D1) in the path between the TVS and the buck input should limit this, but the Zener's current handling during transient events needs verification — if it can't shunt the full transient current, the buck sees overvoltage.
- **Push-pull buzzer driver uses two NPNs:** A standard complementary push-pull uses NPN + PNP. Two MMBT3904 (both NPN) in totem-pole means the high-side transistor loses ~1.5 V (`Vce_sat + Vbe`), reducing drive voltage to the buzzer. A PNP high-side or a single N-FET low-side switch would deliver full rail voltage and be simpler.
- **No ESD protection on signal pins:** XIAO GPIO pins (D0, D1, D3) connect directly to external components with no TVS or series resistance. Scooter environment = metal frame, outdoor exposure, ESD events. Small TVS diodes (e.g., PESD5V0S2BT) on latch and buzzer lines would add robustness.
- **No conformal coating specified:** The board sits on a scooter deck exposed to rain, road spray, and vibration. Conformal coating or potting should be specified in production documentation.

## 7. Consolidated Recommendations


| Area     | Change                                                                                    | Source        |
| -------- | ----------------------------------------------------------------------------------------- | ------------- |
| Crypto   | Use full CCM encryption, not auth-only                                                    | Opus          |
| Crypto   | Add random session diversifier to nonce bytes 4-12                                        | Opus          |
| Crypto   | Check `ccm_mic_8()` return value in all call sites                                        | Opus          |
| Crypto   | Utilize hardware `NRF_CCM` instead of manual software ECB loops                           | Gemini & Opus |
| Storage  | Read both current + old log in `load()` to prevent power-failure vulnerabilities          | Opus          |
| Storage  | Replace LittleFS with FDS or raw circular buffer                                          | Gemini        |
| Storage  | Use nRF52 Retention Registers for zero-cost caching                                       | Gemini        |
| Firmware | Decouple BLE callback from blocking hardware actuation via FreeRTOS queue                 | Gemini        |
| Firmware | Add WDT on Guillemot                                                                      | Opus          |
| Firmware | Reduce button timeout to 2-3 s                                                            | Opus          |
| Firmware | Compensate long-press threshold for boot time                                             | Opus          |
| Firmware | Replace FreeRTOS LED task with timer/non-blocking approach                                | Opus          |
| Protocol | Filter by registered Company ID or magic byte instead of 0xFFFF                           | Opus          |
| Protocol | Add delivery confirmation to provide user feedback                                        | Opus          |
| Hardware | Correct technical documentation regarding MOSFET topology, 10V rail, and gate protections | Gemini        |
| Hardware | Add reverse polarity protection                                                           | Opus          |
| Hardware | Verify TVS/Zener transient current path                                                   | Opus          |
| Hardware | Replace NPN totem-pole buzzer driver with complementary push-pull or low-side N-FET       | Opus          |
| Hardware | Add ESD protection on exposed GPIO                                                        | Opus          |
| Hardware | Specify conformal coating in production documentation                                     | Opus          |


