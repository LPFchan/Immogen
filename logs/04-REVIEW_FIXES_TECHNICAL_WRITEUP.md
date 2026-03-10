# Technical Fixes for Codebase Review Findings

*Date: 2026-03-09 06:21*

*Proposed solutions for all valid findings from CODEBASE_REVIEW_FINDINGS.md.*

---

## 1. Unprovisioned Receiver (Guillemot) — **HIGH**

**Problem:** Unprovisioned receiver accepts commands; attacker with zero key can craft valid MIC.

**Fix:** Add early return in `scan_callback` before `verify_payload`:

```cpp
void scan_callback(ble_gap_evt_adv_report_t* report) {
  immo::Payload pl{};
  if (!parse_payload_from_report(report, pl)) return;
  if (key_is_all_zeros()) return;  // Reject when unprovisioned
  if (!verify_payload(pl)) return;
  handle_valid_command(pl);
}
```

**File:** `Guillemot/firmware/guillemot/src/main.cpp`

---

## 2. Uguisu Counter Persistence Order — **HIGH**

**Problem:** `g_store.update(counter)` runs after `delay(UGUISU_ADVERTISE_MS)`; power loss during that window causes counter reuse and replay on next boot.

**Fix:** Persist counter before advertising (or at least before the long delay):

```cpp
// Persist first; if storage fails, abort before advertising
g_store.update(counter);
start_advertising_once(MSD_COMPANY_ID, payload13);
delay(UGUISU_ADVERTISE_MS);
system_off();
```

If you need to avoid advancing counter before advertising succeeds, alternative: persist at the *start* of the advertise phase (before `delay`), so the ~2s window is eliminated—worst case is power loss mid-advertise, but counter is already saved.

**File:** `Uguisu/firmware/uguisu/src/main.cpp`

---

## 3. Guillemot Lock/Counter Order — **MEDIUM**

**Problem:** Latch/buzzer actions run before `g_store.update()`. If update fails, physical state changed but counter not persisted; next unlock may be rejected as replay.

**Fix:** Update counter before or as close as possible to the physical action. Option A—update first, then act:

```cpp
case immo::Command::Unlock:
  g_store.update(pl.counter);
  latch_set_pulse();
  buzzer_tone_ms(BUZZER_UNLOCK_MS);
  break;
case immo::Command::Lock:
  g_store.update(pl.counter);
  buzzer_tone_ms(BUZZER_LOCK_MS);
  latch_reset_pulse();
  break;
```

**Caveat:** If `update()` fails, you could add error handling (e.g. retry, abort, or document that failure leaves system in undefined state). Document failure behavior in comments or design doc.

**File:** `Guillemot/firmware/guillemot/src/main.cpp`

---

## 4. Stricter Checksum Validation (immo_provisioning) — **MEDIUM**

**Problem:** `strlen(checksum_hex) < 4` allows trailing garbage; only first 4 hex chars are validated.

**Fix:** Require exactly 4 hex chars. After parsing `checksum_hex`:

```cpp
// Replace: strlen(checksum_hex) < 4
// With: require exactly 4 hex chars
if ((size_t)(col1 - key_hex) != 32 || (size_t)(col2 - counter_hex) != 8) {
  Serial.println("ERR:MALFORMED");
  return false;
}
// Validate checksum is exactly 4 hex chars
const char* checksum_hex = col2 + 1;
if (strlen(checksum_hex) != 4) {
  Serial.println("ERR:MALFORMED");
  return false;
}
for (int i = 0; i < 4; i++) {
  char c = checksum_hex[i];
  if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
    Serial.println("ERR:MALFORMED");
    return false;
  }
}
```

Or use a helper that returns false if any char after the 4th is non-whitespace/non-null.

**File:** `ImmoCommon/src/immo_provisioning.cpp` (and mirrored copies in Guillemot/Uguisu libs)

---

## 5. Document BLE Scan/Advert Timing — **MEDIUM**

**Problem:** Guillemot 1% duty cycle scan can miss Uguisu adverts; worst-case latency can reach several seconds.

**Fix:** Documentation only. Add to design/architecture docs:

- Guillemot: `SCAN_INTERVAL_MS=2000`, `SCAN_WINDOW_MS=20` (1% duty cycle).
- Uguisu: adverts every ~100 ms for ~2 s.
- Worst-case: if scan window falls between adverts, user may need to hold button and retry; worst-case latency ~2–4 seconds.
- Recommend documenting this in user-facing docs (e.g. “hold fob button for 2–3 seconds near MCU”).

**Files:** `Guillemot/README.md`, `Uguisu/README.md`, or `logs/CODEBASE_TECHNICAL_WRITEUP.md`

---

## 6. ensure_provisioned Timeout Option — **LOW**

**Problem:** When VBUS present and not provisioned, loop runs indefinitely; can block boot when USB plugged but no host connected.

**Fix:** Add optional max attempts or cumulative timeout. For example:

```cpp
void ensure_provisioned(
    uint32_t timeout_ms,
    bool (*on_success)(...),
    void (*load_provisioning)(),
    bool (*is_provisioned)(),
    uint32_t max_attempts = 0  // 0 = infinite (current behavior)
) {
  if (prov_is_vbus_present()) {
    prov_run_serial_loop(timeout_ms, on_success);
    if (load_provisioning) load_provisioning();
  }
  uint32_t attempts = 0;
  while (is_provisioned && !is_provisioned() && prov_is_vbus_present()) {
    if (max_attempts > 0 && ++attempts >= max_attempts) break;
    prov_run_serial_loop(timeout_ms, on_success);
    if (load_provisioning) load_provisioning();
  }
}
```

Callers who need non-blocking behavior can pass e.g. `max_attempts=3`; default `0` preserves current behavior.

**File:** `ImmoCommon/src/immo_provisioning.cpp`

---

## 7. load() Partial-Record Handling — **LOW**

**Problem:** Truncated write could leave half-record; `read` returning partial bytes exits loop but no explicit detection or recovery.

**Fix:** Add basic detection and optional recovery:

```cpp
void CounterStore::load() {
  last_counter_ = 0;
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(log_path_, ...));
  if (!f) return;

  CounterRecord rec{};
  while (true) {
    const size_t n = f.read(reinterpret_cast<void*>(&rec), sizeof(rec));
    if (n == 0) break;
    if (n != sizeof(rec)) {
      // Partial record; stop to avoid using corrupt data
      break;
    }
    if (record_crc(rec.counter) != rec.crc32) continue;
    last_counter_ = rec.counter;
  }
}
```

Optional: if partial read detected, log or set a “dirty” flag for diagnostics. Avoid using the partial record.

**File:** `ImmoCommon/src/immo_storage.cpp`

---

## 8. rotateIfNeeded_ When Log Doesn't Exist — **LOW**

**Problem:** Behavior of `f.size()` when file doesn't exist should be verified.

**Fix:** Adafruit LittleFS `open` with `FILE_O_READ` on non-existent file typically returns invalid/falsy handle; `if (!f) return` already guards. Add an explicit check for clarity:

```cpp
void CounterStore::rotateIfNeeded_() {
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(log_path_, FILE_O_READ));
  if (!f) return;  // File doesn't exist or open failed
  const size_t sz = f.size();
  f.close();
  // ...
}
```

Document in comment: “When log file doesn't exist, open fails and we return early; f.size() is never called.”

**File:** `ImmoCommon/src/immo_storage.cpp`

---

## 9. serial.js: Document No Concurrent Use — **LOW**

**Problem:** Multiple concurrent `readLineWithTimeout` calls resolve in FIFO order; lines can be routed to wrong consumer.

**Fix:** Documentation only. Add to `serial.js` or `Whimbrel` README:

```js
/**
 * readLineWithTimeout: Reads one line with optional timeout.
 * WARNING: Concurrent calls are not supported. Lines are delivered FIFO to
 * the first waiter; use sequentially (e.g. send request → readLine → send next).
 */
```

**File:** `Whimbrel/js/serial.js`

---

## 10. dfu.js: Document Response Matching Fragility — **LOW**

**Problem:** Responses matched by opcode only; some Nordic DFU ops emit multiple responses; out-of-order or duplicate responses could misroute.

**Fix:** Documentation only. Add comment or docblock:

```js
/**
 * receiveResponse: Matches by opcode only. Some Nordic DFU operations
 * (e.g. PRN) may emit multiple responses. Acceptable for standard
 * single-request flows; may misroute in edge cases with concurrent or
 * duplicate responses.
 */
```

**File:** `Whimbrel/js/dfu.js`

---

## Summary Table

| Priority | Issue | Fix Type | Location |
|----------|-------|----------|----------|
| High | Unprovisioned receiver | Code | Guillemot main.cpp |
| High | Uguisu counter persistence order | Code | Uguisu main.cpp |
| Medium | Guillemot lock/counter order | Code | Guillemot main.cpp |
| Medium | Stricter checksum validation | Code | immo_provisioning.cpp |
| Medium | BLE scan/advert timing | Docs | Architecture/README |
| Low | ensure_provisioned timeout | Code | immo_provisioning.cpp |
| Low | load() partial-record handling | Code | immo_storage.cpp |
| Low | rotateIfNeeded_ file existence | Code + comment | immo_storage.cpp |
| Low | serial.js concurrency | Docs | serial.js |
| Low | dfu.js response matching | Docs | dfu.js |

---

## Implementation Notes

- **ImmoCommon copies:** Guillemot and Uguisu each have `lib/ImmoCommon`; changes to provisioning/storage should be made in the root `ImmoCommon/` and propagated (or use a single source via submodule/symlink).
- **Testing:** After fixes, verify: (1) unprovisioned Guillemot rejects adverts, (2) Uguisu survives power loss during advertise, (3) provisioning flow still works, (4) lock/unlock with storage failure scenarios if testable.
