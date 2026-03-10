# Architecture Review — Resolution Summary

*Date: 2026-03-09 22:49*

This document records how each concern raised in [7-ARCHITECTURE_REVIEW.md](7-ARCHITECTURE_REVIEW.md) was addressed.

---

## High Severity

### BLE Scan Duty Cycle Too Low — **RESOLVED**

**Original concern:** `SCAN_INTERVAL_MS = 2000` with `SCAN_WINDOW_MS = 20` yielded a 1% duty cycle. Detection probability was unacceptably low.

**Resolution:** Scan parameters updated in `Guillemot/firmware/guillemot/include/guillemot_nrf52840.h`:

```cpp
static constexpr uint16_t SCAN_INTERVAL_MS = 500;  // was 2000
static constexpr uint16_t SCAN_WINDOW_MS = 20;
```

New duty cycle: 20/500 = 4%.

**Phase-based model:** The original review used an independence assumption (P(miss all) = 0.99^20), treating each packet as an independent trial. This is misleading. In reality, the phase offset between the fob's advertising clock and the receiver's scan clock is fixed at button press; both clocks run deterministically for the burst. With `advInterval << scanInterval` (few packets per scan cycle), some phases always miss; with `advInterval >> scanInterval` (many packets per scan cycle), detection is near-certain. The new settings (25 packets per 500 ms scan cycle) put the system in the dense-packet regime. The [BLE timing simulator](../../Uguisu/tools/ble_timing_simulator.html) models phase correctly and shows ~100% detection for these parameters.

---

## Medium Severity

### No LED Feedback on Fob — **RESOLVED**

**Original concern:** RGB LED pins were defined but never driven. No visual confirmation of button press or command type.

**Resolution:** Uguisu now has full LED feedback via `Uguisu/firmware/uguisu/include/led_effects.h`:

- **Green flash** — Unlock command
- **Red flash** — Lock command  
- **Provisioning pulse** — Blue blink when waiting for serial provisioning (VBUS present)
- **Low-battery warning** — Pulsed red when voltage below threshold

Implementation in `Uguisu/firmware/uguisu/src/main.cpp`:
- `cmd_pin` selects `PIN_LED_R` or `PIN_LED_G` based on command
- `led::flash_once()` or `led::flash_low_battery()` provides feedback after advertising starts

---

### 30-Second Boot Delay When USB-Powered — **ACCEPTED AS DESIGNED**

**Original concern:** `ensure_provisioned()` runs `prov_run_serial_loop()` with a 30 s timeout whenever VBUS is detected, even when already provisioned. USB-powered boots stall for 30 s.

**Resolution:** This behavior is intentional. When VBUS is present, the device always listens for a `PROV:` command so the user can re-provision without a button hold. The 30 s window is the trade-off for that UX. Battery-powered boots are unaffected.

---

## Low Severity

### CounterStore::load() Should Track Max, Not Last — **RESOLVED**

**Original concern:** `load()` took the last valid record instead of the maximum. Flash corruption could roll the counter back and open a replay window.

**Resolution:** `ImmoCommon/src/immo_storage.cpp` line 45:

```cpp
if (rec.counter > last_counter_) last_counter_ = rec.counter;
```

Now tracks the maximum valid counter across all records.

---

### Dead Code: le16_write() — **RESOLVED**

**Original concern:** `le16_write()` was defined but never called. Wrong endianness for CCM B0 block anyway.

**Resolution:** Removed from `immo_crypto.cpp`. B0 block message length continues to be written as big-endian manually (lines 55–56). Only `le32_write()` remains for nonce/msg.

---

### Redundant Counter Log Removal — **RESOLVED**

**Original concern:** Guillemot's `on_provision_success` called `InternalFS.remove()` on counter log paths before `g_store.seed()`, which already removes both files.

**Resolution:** Guillemot's `on_provision_success` now delegates directly to `immo::prov_write_and_verify()`, which calls `store.seed(counter)`. No redundant `InternalFS.remove()` calls.

---

### Float Division for Ad Interval — **RESOLVED**

**Original concern:** `UGUISU_ADV_INTERVAL_MS / 0.625` used floating point unnecessarily on embedded.

**Resolution:** `Uguisu/firmware/uguisu/src/main.cpp` line 55 uses integer math:

```cpp
Bluefruit.Advertising.setInterval((UGUISU_ADV_INTERVAL_MS * 8 + 4) / 5, (UGUISU_ADV_INTERVAL_MS * 8 + 4) / 5);
```

Equivalent conversion (0.625 = 5/8) without floating point.

---

### Code Duplication — **RESOLVED**

**Original concern:** `key_is_all_zeros()` and `led_error_loop()` were duplicated in both guillemot and uguisu main.cpp.

**Resolution:**
- **Key check:** Logic consolidated in `ImmoCommon` as `immo::is_key_blank()`. Both firmwares use thin wrappers: `return immo::is_key_blank(g_psk);`
- **Error loop:** `immo::led_error_loop(int led_pin)` in `ImmoCommon/src/immo_util.cpp`. Guillemot calls `immo::led_error_loop(PIN_ERROR_LED)`. Uguisu uses `led::error_loop()` from `led_effects.h`, which drives the RGB LED for its hardware—different API for different hardware, not duplication.

---

## Design Constraints (Not Bugs)

The following items were documented as inherent to the architecture. No changes made:

| Constraint | Note |
|------------|------|
| No key rotation | Compromised key requires physical USB re-provisioning of both devices |
| Counter overflow at `0xFFFFFFFF` | ~136 years at 1 press/s; system bricks silently |
| Command byte in plaintext | CCM authenticates only; eavesdropper can see lock vs unlock |

---

## Summary

| Item | Status |
|------|--------|
| BLE scan duty cycle | Resolved |
| LED feedback on fob | Resolved |
| 30 s USB boot delay | Accepted as designed |
| CounterStore track max | Resolved |
| Dead code le16_write | Resolved |
| Redundant counter removal | Resolved |
| Float division | Resolved |
| Code duplication | Resolved |
