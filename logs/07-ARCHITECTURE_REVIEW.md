# BLE Immobilizer — Architecture Review Findings

*Date: 2026-03-09 14:43*

Issues identified during full codebase review, ranked by severity.

---

## High Severity

### BLE Scan Duty Cycle Too Low

**Location:** `guillemot_config.example.h`

`SCAN_INTERVAL_MS = 2000` with `SCAN_WINDOW_MS = 20` yields a **1% duty cycle**. The fob sends ~20 advertisements (every 100ms for 2s). Each has a 1% chance of landing in the scan window.

- **P(miss all 20) = 0.99^20 ≈ 81.8%**
- Only ~18% chance of hearing a single fob press
- Users would need 4–5 presses on average

Typical responsive BLE systems use 50%+ duty cycle (e.g., 100ms interval / 50ms window).

---

## Medium Severity

### No LED Feedback on Fob

**Location:** `uguisu/main.cpp`

RGB LED pins (`PIN_LED_B`, `PIN_LED_R`, `PIN_LED_G`) are defined in config but never driven. The user gets zero visual confirmation of button press, command type, or transmission. A brief flash (green = unlock, red = lock) would be trivial to add.

### 30-Second Boot Delay When USB-Powered

**Location:** `immo_provisioning.cpp:133–136`

`ensure_provisioned()` always runs `prov_run_serial_loop()` with a 30s timeout when VBUS is detected, even if already provisioned. Any USB-powered boot (e.g., debugging with serial monitor) stalls for 30 seconds before scanning starts. Re-provisioning should require an explicit trigger (e.g., button hold) rather than just USB presence.

---

## Low Severity

### `CounterStore::load()` Should Track Max, Not Last

**Location:** `immo_storage.cpp:34–45`

```cpp
while (f.read(&rec, sizeof(rec)) == sizeof(rec)) {
    if (record_crc(rec.counter) != rec.crc32) continue;
    last_counter_ = rec.counter;  // Takes last valid, not maximum
}
```

If flash corruption produces a valid-looking low-value record after a high one, the counter rolls back, opening a replay window. Fix: `if (rec.counter > last_counter_) last_counter_ = rec.counter;`

### Dead Code: `le16_write()`

**Location:** `immo_crypto.cpp:12–15`

Defined in anonymous namespace, never called. The B0 block writes message length as big-endian manually (lines 60–61), and `le16_write` writes little-endian — wrong endianness for CCM B0 anyway.

### Redundant Counter Log Removal

**Location:** `guillemot/main.cpp:47–50`

```cpp
InternalFS.remove(COUNTER_LOG_PATH);      // Redundant
InternalFS.remove(OLD_COUNTER_LOG_PATH);   // Redundant
g_store.seed(counter);                     // seed() already removes both files
```

Uguisu's `on_provision_success()` does not have this redundancy.

### Float Division for Ad Interval

**Location:** `uguisu/main.cpp:104`

```cpp
Bluefruit.Advertising.setInterval(UGUISU_ADV_INTERVAL_MS / 0.625, ...);
```

Unnecessary floating point on embedded. Use integer math `(UGUISU_ADV_INTERVAL_MS * 8 + 4) / 5`, or use the existing `setIntervalMS()` API which takes milliseconds directly.

### Code Duplication

`key_is_all_zeros()` and `led_error_loop()` are identical in both `guillemot/main.cpp` and `uguisu/main.cpp`. Both belong in ImmoCommon.

---

## Design Constraints (Not Bugs)

These are inherent to the architecture, not fixable without redesign:

| Constraint | Note |
|------------|------|
| No key rotation | Compromised key is permanent; requires physical USB re-provisioning of both devices |
| Counter overflow at `0xFFFFFFFF` | System silently bricks (~136 years at 1 press/s); `counter = last + 1` wraps to 0, receiver rejects 0 ≤ 0xFFFFFFFF |
| Command byte in plaintext | CCM authenticates but doesn't encrypt; eavesdropper can see lock vs unlock (acceptable for threat model) |
