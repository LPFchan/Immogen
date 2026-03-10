# Immogen Architecture Evaluation & Fix Plan

*Date: 2026-03-10 09:14*

This document evaluates the architectural critiques from the combined review (`21-ARCHITECTURE_CRITIQUE_COMBINED.md`), filters out invalid claims, and outlines actionable fixes for the valid issues.

## 1. Uguisu (Key Fob) Firmware

### The LittleFS Power Drain
*   **Problem:** On every wake from `system_off`, `immo_storage::load()` sequentially reads the entire LittleFS counter log to find the latest anti-replay counter, wasting CPU cycles and battery.
*   **Validity:** **Valid.** `immo_storage.cpp` uses a linear `while (f.read(...))` loop. Since the fob restarts from deep sleep on every press, this linear scan happens on every use.
*   **Fix:** Utilize nRF52 General Purpose Retention Registers (`GPREGRET`) or `.noinit` RAM to cache the counter across `system_off` sleeps. Only scan LittleFS on a cold boot (battery insertion). Alternatively, use a raw flash page with a binary search instead of a full filesystem.

### Button Press Timing Offset
*   **Problem:** `wait_for_button_press_release()` runs after boot initialization (~100-200 ms). The physical press is longer than the measured press, making the 1000 ms long-press threshold effectively ~1200 ms.
*   **Validity:** **Valid.** The nRF52 wakes from `system_off()` with a reset. `setup()` takes time before polling the button.
*   **Fix:** Subtract an estimated boot time offset (e.g., 150 ms) from the duration calculated in `wait_for_button_press_release()`, or simply document the true physical threshold for users.

### Excessive Button Timeout
*   **Problem:** The 10-second wait for button release (`UGUISU_BUTTON_TIMEOUT_MS`) drains battery unnecessarily if a button is accidentally held down in a pocket.
*   **Validity:** **Valid.** The system stays awake drawing ~5 mA for up to 10 seconds.
*   **Fix:** Reduce `UGUISU_BUTTON_TIMEOUT_MS` from 10000 to 2000-3000 ms.

### Overkill FreeRTOS Task for Provisioning LED
*   **Problem:** Uguisu spawns a dedicated FreeRTOS task with a 512-byte stack just to blink a blue LED during USB provisioning.
*   **Validity:** **Valid.** While `ensure_provisioned()` blocks the main loop reading serial, an entire FreeRTOS task for an LED is heavy.
*   **Fix:** Use a non-blocking hardware timer/interrupt approach, or simply accept the minor overhead since it only occurs when plugged into USB power.

## 2. Guillemot (Receiver) Firmware

### Blocking the BLE Stack
*   **Problem:** `handle_valid_command()` blocks for hundreds of milliseconds using `buzzer_tone_ms()` (which uses `delay()`) from within the Bluefruit `scan_callback`.
*   **Validity:** **Valid.** `scan_callback` runs in a high-priority context. Blocking it stalls the BLE scanner, causing dropped packets.
*   **Fix:** Decouple actuation. Have `scan_callback` push valid commands to a FreeRTOS queue. A dedicated `ImmoTask` should consume the queue and handle latch/buzzer timing asynchronously.

### Missing Watchdog Timer (WDT)
*   **Problem:** Guillemot is an always-on safety device. If the firmware hangs, it stays in its current state forever.
*   **Validity:** **Valid.** No WDT is initialized in `Guillemot/firmware/src/main.cpp`.
*   **Fix:** Enable the nRF52840 hardware WDT with a suitable timeout (e.g., 5 seconds) and pet it in the idle loop or a dedicated task.

## 3. Cryptography & Security

### `verify_payload()` Not Checking `ccm_mic_8()` Return Value
*   **Problem:** Claimed that `verify_payload()` ignores the return value of `ccm_mic_8()`, potentially comparing against an uninitialized buffer if the SoftDevice ECB call fails.
*   **Validity:** **INVALID (BULLSHIT).** The codebase explicitly checks the return value in `Guillemot/firmware/src/main.cpp` line 64: `if (!immo::ccm_mic_8(g_psk, nonce, msg, sizeof(msg), expected)) return false;`. This claim is completely false and has been discarded.

### Missing Session Diversifier in Nonce
*   **Problem:** The CCM nonce pads 9 bytes with zeros. If a key is reused but the counter resets, the nonce repeats, breaking AES-CCM security.
*   **Validity:** **Valid.**
*   **Fix:** Generate 9 random bytes during the USB provisioning process, store them alongside the 16-byte key, and use them as the nonce suffix instead of zeros.

### Unencrypted Payload
*   **Problem:** The command and counter are sent in plaintext. An eavesdropper can see usage frequency and lock/unlock actions.
*   **Validity:** **Valid.** The implementation only uses CCM for authentication (CBC-MAC), not confidentiality.
*   **Fix:** Apply the CCM keystream to encrypt the 5-byte payload.

### Manual CCM vs. Hardware/SoftDevice
*   **Problem:** The crypto implementation manually loops ECB blocks instead of using the nRF52's hardware CCM or SoftDevice CCM API.
*   **Validity:** **Valid.** `immo_crypto.cpp` does manual CBC-MAC and CTR mode using `sd_ecb_block_encrypt()`.
*   **Fix:** Migrate to `sd_ble_opt_set` or `NRF_CCM` hardware peripheral to reduce active CPU time and manual cryptographic code.

## 4. Anti-Replay & Storage

### Power-Failure Vulnerability in Log Rotation
*   **Problem:** `rotateIfNeeded_()` renames `log` to `old_log`. If power fails before the new `log` is written, `load()` finds no `log` file and resets the counter to 0, opening a massive replay vulnerability.
*   **Validity:** **Valid.** `immo_storage.cpp`'s `load()` function only opens `log_path_`. It never checks `old_log_path_` if the primary log is missing.
*   **Fix:** Modify `load()` to read *both* `log_path_` and `old_log_path_`, taking the maximum valid counter found across both files.

## 5. BLE Protocol

### Unregistered Company ID 0xFFFF
*   **Problem:** 0xFFFF triggers unnecessary MIC verification attempts for any unregistered BLE device nearby.
*   **Validity:** **Valid.**
*   **Fix:** Assign a unique random 16-bit identifier or use a magic byte sequence in the payload to filter out non-Immogen advertisements early.

### No Delivery Confirmation
*   **Problem:** Fob has no way of knowing if the receiver actually heard the command.
*   **Validity:** **Valid, but accepted limitation.**
*   **Fix:** Implementing two-way communication requires the fob to switch to scanning mode, which adds latency and power draw. The scooter's buzzer provides auditory confirmation. This may be left as-is to preserve battery.

## 6. Hardware & Documentation

### Hardware Documentation Errors
*   **Problem:** The technical documentation misrepresents the Guillemot schematic (e.g., calling Q1 a high-side switch instead of low-side, misunderstanding the 10V gate drive and Vgs clamping).
*   **Validity:** **Valid.** The schematic uses a low-side N-FET (IPB042N10N3G) with the source tied to ESC- (GND return), and the 10V rail is indeed used to drive the gate.
*   **Fix:** Rewrite Section 6 of `18-CODEBASE_TECHNICAL_DOCUMENTATION.md` to accurately reflect the schematic topology.

### Missing Hardware Protections (Guillemot)
*   **Problem:** Missing reverse polarity protection, potential TVS overvoltage on buck, sub-optimal NPN push-pull, lack of ESD protection.
*   **Validity:** **Valid.**
*   **Fix:** Update the KiCad design in a future hardware revision (e.g., add a P-FET ideal diode, use a complementary NPN/PNP pair for the buzzer, add ESD TVS arrays on exposed GPIOs).
