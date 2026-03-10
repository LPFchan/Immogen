# Immogen Architecture Critique and Improvement Proposals

This document outlines structural flaws, vulnerabilities, and ground-up restructuring proposals for the Immogen project. 

## 1. Module and Function Level Inefficiencies & Vulnerabilities

### 1. Receiver (Guillemot) Denial of Service (DoS) Vulnerability
- **Flaw:** `verify_payload` executes cycle-heavy AES-128-CCM decryption before verifying the plaintext monotonic counter.
- **Risk:** Spamming old payloads exhausts MCU resources, risking FreeRTOS starvation or WDT resets.
- **Fix:** Extract and validate the plaintext counter before initiating decryption.
- **Verdict:** **VALID**. The multi-block decryption before counter validation is a genuine DoS vector.

### 2. Shared Cryptography (`immo_crypto.cpp`)
- **Flaw:** `constant_time_eq` uses a bitwise OR loop susceptible to aggressive compiler optimizations.
- **Risk:** Compilers can introduce early exits, breaking constant-time guarantees and enabling timing attacks.
- **Fix:** Use `#pragma GCC optimize ("O0")` or `volatile` pointers to enforce constant-time execution.
- **Verdict:** **VALID**. Compilers frequently optimize such loops into non-constant-time checks.

### 3. Fob (Uguisu) Blocking Delays
- **Flaw:** `wait_for_button_press_release()` continuously polls the button using `delay(10)`.
- **Risk:** Waking up every 10ms wastes coin-cell battery life.
- **Fix:** Refactor using a hardware GPIO interrupt (`sd_app_evt_wait()`) to sleep indefinitely.
- **Verdict:** **VALID**. Interrupt-driven events are significantly more power-efficient than polling loops.

### 4. Missing Anti-Bricking Measures
- **Flaw:** Both devices enter an infinite `led::error_loop()` if the internal LittleFS fails.
- **Risk:** Filesystem corruption permanently bricks the hardwired scooter receiver.
- **Fix:** Implement a graceful fallback or secondary BLE OTA recovery.
- **Verdict:** **VALID**. Sudden power loss can corrupt flash, completely disabling the scooter without recovery options.

## 2. Structural and Big Picture Restructuring

### 1. Monorepo vs. PlatformIO Fragility
- **Flaw:** Separate PlatformIO environments rely on brittle `symlink://` dependencies.
- **Risk:** Breaks cross-platform compilation and CI/CD pipelines (especially on Windows).
- **Fix:** Restructure as a unified monorepo using `src_filter` or build flags.
- **Verdict:** **VALID**. Monorepos are standard practice to avoid PlatformIO's fragile symlink handling.

### 2. Receiver Main Loop Blocking
- **Flaw:** The BLE `scan_callback` executes synchronous hardware delays (e.g., `buzzer_tone_ms`).
- **Risk:** Blocking the high-priority BLE thread prevents SoftDevice event processing, risking dropped packets and panics.
- **Fix:** Decouple hardware logic to an asynchronous state machine in the main `loop()`.
- **Verdict:** **VALID (CRITICAL)**. Blocking RTOS callbacks with delays is a severe architectural anti-pattern.

## 3. "Ground-Up" Architectural Alternatives

### 1. BLE GATT Challenge-Response (Eliminating Flash Wear)
- **Current Issue:** The connectionless monotonic counter strategy forces constant flash writes, degrading memory over time.
- **Alternative:** Use a connected BLE GATT Challenge-Response mechanism with randomized nonces.
- **Verdict:** **VALID**. This eliminates flash wear entirely and inherently prevents state desynchronization.

### 2. Hardware Power Control: Bistable Relays
- **Current Issue:** The discrete SR Latch requires pull-downs and is sensitive to logic pin floating during MCU resets.
- **Alternative:** Replace with a mechanical Bistable Latching Relay.
- **Verdict:** **VALID**. Bistable relays retain state mechanically with zero quiescent current and are immune to logic instability.

### 3. Failsafe Emergency Access
- **Current Issue:** Losing the paired fob permanently strands the scooter.
- **Alternative:** Embed a secondary GATT characteristic for an "Emergency PIN Unlock" via mobile app.
- **Verdict:** **VALID**. An app-based fallback is a standard and necessary failsafe for automotive immobilizers.

### 4. Hardware Secure Element (SE)
- **Current Issue:** Raw AES keys are stored in LittleFS, vulnerable to physical extraction via SWD/JTAG.
- **Alternative:** Utilize a dedicated cryptographic co-processor (e.g., ATECC608A).
- **Verdict:** **VALID**. Because the receiver is physically accessible, keys should be isolated in secure hardware.
