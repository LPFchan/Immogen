# Architecture Fixes Summary

*Date: 2026-03-10 10:06*

This document summarizes the architectural issues identified during the system evaluation, outlining what the issues were, why they were problematic, and how we decided to address or skip them.

## Part 1: Implemented Fixes

### 1. Hardware Documentation Inaccuracies
- **What was the issue:** The technical documentation (`18-CODEBASE_TECHNICAL_DOCUMENTATION.md`) misrepresented the Guillemot PCB's MOSFET topology, 10V rail purpose, and P-FET gate protection.
- **Why it was an issue:** The documented design was electrically impossible (e.g., describing a 10V gate drive turning on a high-side N-FET with a 42V source, which would require Vg > 46V).
- **How we fixed it:** We created `24-HARDWARE_DOCUMENTATION_CORRECTIONS.md` to correctly describe the actual design: a low-side load switch topology, a 10V gate drive derived from the battery, and a 12V Zener clamping the gate-source voltage for protection.

### 2. Button Timing & UX Limitations
- **What was the issue:** Boot time (~100-200ms) caused the measured button press duration to be shorter than the physical press. Extremely short presses (< 200ms) were ignored, long presses provided no LED feedback until release, and the timeout was excessively long (10 seconds).
- **Why it was an issue:** It created a sluggish and unpredictable user experience, with delayed feedback or missed commands.
- **How we fixed it:** 
  - Reduced the long-press threshold to 700ms and the timeout to 2000ms.
  - Updated `wait_for_button_press_release()` to instantly return a short duration if the button pin is already HIGH after boot (capturing <200ms presses).
  - Implemented an early exit from the polling loop once the duration reaches 700ms, providing instant LED feedback exactly when the long-press threshold is met.

### 3. Log Rotation Power-Failure Vulnerability
- **What was the issue:** The LittleFS counter log rotation process `rotateIfNeeded_()` renamed the current log to an old log, then created a new one. 
- **Why it was an issue:** If a power failure occurred immediately after the rename but before the new log was written, the `load()` function would find no active log, default the counter to 0, and permanently reset the counter sequence. This exposed the system to replay attacks.
- **How we fixed it:** Modified `CounterStore::load()` to scan both the active log path and the old log path on boot, taking the maximum valid counter found across both files.

### 4. MSD Magic Bytes
- **What was the issue:** The system used `0xFFFF` (the Bluetooth SIG test ID) for its Manufacturer Specific Data (MSD) company ID.
- **Why it was an issue:** Other random devices in the wild using the `0xFFFF` test ID could trigger full MIC checks on the receiver, wasting power and CPU cycles.
- **How we fixed it:** Injected a 2-byte magic prefix (`0x494D` or `"IM"`) immediately after the company ID in the MSD payload. The receiver now filters for this magic sequence before proceeding to cryptographic verification.

### 5. Payload Plaintext Leakage
- **What was the issue:** The 5-byte payload (4-byte counter + 1-byte command) was authenticated via CCM-MAC but not encrypted.
- **Why it was an issue:** A passive sniffer could read the plaintext to see which command (lock/unlock) was sent and observe the counter to track usage frequency.
- **How we fixed it:** Replaced the pure CBC-MAC generation with full CCM authenticated encryption. The counter is passed as unencrypted Additional Authenticated Data (AAD) so the receiver can derive the nonce, while the 1-byte command is encrypted using the AES-CTR keystream.

### 6. Lack of Watchdog Timer (WDT)
- **What was the issue:** The Guillemot receiver had no Watchdog Timer configured.
- **Why it was an issue:** If the firmware hung (e.g., SoftDevice error or flash stall), the MCU would sleep forever.
- **How we fixed it:** Configured the nRF52840 hardware WDT with an 8-second timeout, continuously fed by the main `loop()`. If a hang occurs, the MCU reboots cleanly. Because Guillemot uses a hardware SR latch, the scooter does not lose power mid-ride; the hardware latch physically maintains the "unlocked" state while the firmware restarts.

---

## Part 2: Issues Not Pursued (Skipped/Rejected)

### 7. LittleFS Power Drain
- **What was the issue:** It was claimed that linearly scanning up to 512 LittleFS records on every button press wastes CPU and power.
- **Why it was an issue:** Reading flash sequentially takes a few milliseconds, slightly delaying the BLE broadcast.
- **Why we skipped it:** The full scan takes only ~2-5ms, which is completely negligible compared to the ~100-200ms boot time. Adding a `.noinit` RAM cache would unnecessarily complicate the code for a tiny, immaterial power saving.

### 8. Blocking the BLE Stack
- **What was the issue:** `handle_valid_command()` plays buzzer tones using a blocking `delay()`, stalling the BLE scanner for ~275ms.
- **Why it was an issue:** The scanner cannot receive packets while blocked.
- **Why we skipped it:** The fob sends ~30 duplicate packets over a 600ms window. Only the *first* valid packet needs to be received. Blocking after the first packet is accepted is completely fine. Dropping subsequent redundant duplicates doesn't hurt anything. Adding a FreeRTOS queue to decouple this was deemed an unnecessary complication.

### 9. Nonce Lacks Session Diversifier
- **What was the issue:** The nonce lacked a session ID, potentially allowing nonce reuse if the same key is reprovisioned.
- **Why it was an issue:** Nonce reuse in CCM completely breaks the encryption.
- **Why we skipped it:** The system already generates a *new* random key every time it is provisioned. Because the key is always new, the key-nonce pair is inherently globally unique. Adding a session diversifier would be completely redundant.

### 10. FreeRTOS LED Task Overkill
- **What was the issue:** Spawning a FreeRTOS task with 512 bytes of stack just to blink a blue LED during provisioning.
- **Why it was an issue:** Claimed to be a waste of memory and RTOS resources.
- **Why we skipped it:** This code only runs when USB-connected for provisioning, never during battery operation. It has zero impact on battery life or production use, and the memory footprint is trivial on the nRF52840's 256KB RAM.

### 11. Continuous 5% Duty Cycle
- **What was the issue:** The scanner's 25ms/500ms duty cycle draws ~250µA on average.
- **Why it was an issue:** Claimed to be a vampire drain on the scooter battery.
- **Why we skipped it:** At 250µA on a 15Ah scooter battery, it draws about 0.04% per day. The scooter would need to sit for ~7 years to drain from scanning alone. The duty cycle is already well-optimized and perfectly acceptable.

### 12. Manual CCM Instead of Hardware NRF_CCM
- **What was the issue:** The firmware manually implements CCM via ECB loops instead of using the hardware `NRF_CCM` peripheral.
- **Why it was an issue:** Software crypto is theoretically slower and uses more power than hardware crypto.
- **Why we skipped it:** The nRF52840 SoftDevice (S140) reserves the `NRF_CCM` peripheral for its own use; direct application access is blocked. The manual ECB-based CCM implementation is the standard and correct approach when using the SoftDevice.

### 13. `ccm_mic_8()` Return Value Unchecked
- **What was the issue:** The critique claimed the return value of the crypto function wasn't checked.
- **Why it was an issue:** Could potentially lead to accepting an uninitialized or failed MAC.
- **Why we skipped it:** The claim was factually incorrect. The codebase already properly checked the return value and rejected the payload on failure. No fix was needed.

### 14. No Delivery Confirmation
- **What was the issue:** The fob broadcasts blindly for 600ms and goes to sleep without an acknowledgment from the receiver.
- **Why it was an issue:** The user doesn't know for sure if the scooter received the command.
- **Why we skipped it:** This is an inherent architectural tradeoff. Adding an ACK would require a connection or a back-channel scanner on the fob, which would dramatically increase power consumption, complexity, and latency without proportionate benefit.

### 15. Hardware Design Hardening (e.g., Conformal Coating)
- **What was the issue:** The critique pointed out missing production hardening features (reverse polarity protection, TVS overshoot, ESD protection, conformal coating).
- **Why it was an issue:** These are best practices for mass production reliability.
- **Why we skipped it:** While well noted for future hardware revisions, these are not showstoppers for the current prototype design. Implementing these (e.g., conformal coating, schematic restructuring) was explicitly decided against for this iteration.