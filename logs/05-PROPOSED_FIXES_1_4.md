# Proposed Fixes 1–4: Revised Implementations

*Date: 2026-03-09 06:35*

*Fixes derived from CODEBASE_REVIEW_FINDINGS.md, rewritten against actual source.*

---

## Fix 1 — Reject Commands When Unprovisioned (HIGH)

### Problem

When Guillemot has never been provisioned, `g_psk` is all zeros (loaded from
`k_default_psk` by `load_psk_from_storage`). An attacker who knows the system
uses AES-CCM with a zero key can craft a valid MIC for any counter/command pair.
`verify_payload` will pass because it computes the MAC with the same zero key.

### Why it matters

The zero-key state is deterministic and public (it's a hardcoded constant in
source). Any attacker who reads the firmware or knows the protocol can unlock
the immobilizer on an unprovisioned device.

### Fix

`key_is_all_zeros()` already exists at line 54 of `guillemot/src/main.cpp`.
It is currently only used as the `is_provisioned` callback for
`ensure_provisioned`. The fix is a one-line addition to `scan_callback`:

```cpp
// guillemot/src/main.cpp, lines 151-156
void scan_callback(ble_gap_evt_adv_report_t* report) {
  immo::Payload pl{};
  if (!parse_payload_from_report(report, pl)) return;
  if (key_is_all_zeros()) return;          // <-- ADD THIS LINE
  if (!verify_payload(pl)) return;
  handle_valid_command(pl);
}
```

This reuses the existing function with zero new code. The guard sits before
`verify_payload` so we never even attempt cryptographic verification with a
known-bad key.

### Tradeoff

None. An unprovisioned device has no legitimate fob paired to it, so rejecting
all commands is correct behavior.

---

## Fix 2 — Persist Counter Before Advertising on Uguisu (HIGH)

### Problem

In `uguisu/src/main.cpp` lines 191–194, the current order is:

```
start_advertising_once(...)   // line 191 — begin BLE broadcast
delay(UGUISU_ADVERTISE_MS)    // line 192 — wait for receiver to pick it up
g_store.update(counter)       // line 193 — persist incremented counter
system_off()                  // line 194 — deep sleep
```

If power is lost during the `delay` window (battery dies, user pulls battery,
voltage sag), the counter is never persisted. On next boot, `loadLast()` returns
the same value, the fob reuses the same counter, and the signed payload is
identical — a replay. Guillemot will accept it because `pl.counter > last` still
holds (the receiver saw it and persisted, but the fob didn't).

Worse: if the receiver *also* lost power (both devices on the scooter), the
receiver's counter is also stale, and the replayed payload works again. But even
if only the fob loses power, the fob is now stuck — it will keep generating the
same counter that Guillemot already consumed and will reject.

### Fix

Move `g_store.update(counter)` before the advertising call:

```cpp
// uguisu/src/main.cpp, lines 191-194 become:
g_store.update(counter);                        // persist FIRST
start_advertising_once(MSD_COMPANY_ID, payload13);
delay(UGUISU_ADVERTISE_MS);
system_off();
```

### Tradeoff

If `update()` succeeds but advertising fails (BLE stack error, immediate power
loss before any packet transmitted), the counter is advanced but no command
was delivered. The fob "wastes" one counter value. This is harmless — the user
presses the button again, gets counter+2, and everything works. Counter space
is 2^32, so wasting values is not a concern.

The alternative (counter reuse / replay) is a security and reliability failure.
Wasting a counter value is strictly better.

---

## Fix 3 — Persist Counter Before Actuation on Guillemot (MEDIUM)

### Problem

In `guillemot/src/main.cpp` `handle_valid_command` (lines 130–148):

```cpp
case immo::Command::Unlock:
  latch_set_pulse();                 // physical action — 15ms pulse
  buzzer_tone_ms(BUZZER_UNLOCK_MS);  // blocking buzzer — hundreds of ms
  g_store.update(pl.counter);        // persist counter AFTER
  break;
```

If power is lost after `latch_set_pulse` but before `g_store.update`, the
latch has physically moved (scooter unlocked) but the counter is not persisted.
On next boot, `lastCounter()` returns the old value. The same signed payload
from the fob would be accepted again — but the fob has already advanced its own
counter past this value (assuming fix 2), so the real consequence is:

- The receiver's counter is behind the fob's counter.
- The next legitimate fob press (counter N+1) will still work because N+1 > old.
- But if an attacker captured the BLE advertisement for counter N, they can
  replay it after the power loss because the receiver thinks N is still fresh.

This is a narrower window than fix 2, but `buzzer_tone_ms` blocks for the full
buzzer duration (likely 100–500ms), which is a meaningful power-loss window.

### Fix

Persist counter first, then actuate:

```cpp
void handle_valid_command(const immo::Payload& pl) {
  const uint32_t last = g_store.lastCounter();
  if (pl.counter <= last) return;

  // Persist counter BEFORE physical action
  g_store.update(pl.counter);

  switch (pl.command) {
    case immo::Command::Unlock:
      latch_set_pulse();
      buzzer_tone_ms(BUZZER_UNLOCK_MS);
      break;
    case immo::Command::Lock:
      buzzer_tone_ms(BUZZER_LOCK_MS);
      latch_reset_pulse();
      break;
    default:
      break;
  }
}
```

Note: `g_store.update()` is moved above the switch and called once,
eliminating the duplicated call in each case branch.

### Tradeoff

If `update()` succeeds but power dies before `latch_set_pulse`, the counter is
consumed but the latch didn't move. The user presses the fob again (next
counter), and it works. Same "wasted counter" tradeoff as fix 2 — acceptable.

If `update()` itself fails (flash write error), the function still proceeds
to actuate. Whether this is acceptable depends on your failure philosophy:

- **Option A (current approach above):** Actuate anyway. The device responds
  to the user even if storage is degraded. Risk: counter not persisted, replay
  possible on that one value.
- **Option B (strict):** Check `update()` return value, abort if it fails.
  Requires changing `update()` to return `bool`. Safer but means the device
  stops responding when flash is unhealthy.

I recommend Option A for now (matches current behavior, doesn't require API
changes), with a future enhancement to add a return value to `update()`.

---

## Fix 4 — Exact Checksum Length in Provisioning Parser (MEDIUM)

### Problem

In `immo_provisioning.cpp` line 86:

```cpp
if ((size_t)(col1 - key_hex) != 32 || (size_t)(col2 - counter_hex) != 8 || strlen(checksum_hex) < 4) {
```

`strlen(checksum_hex) < 4` accepts any checksum field of 4 or more characters.
Only the first 4 hex characters are consumed by `hex_byte` calls on lines
104–108. Trailing characters are silently ignored.

This means `PROV:...:...:ABCDgarbage` and `PROV:...:...:ABCD` are treated
identically. While not directly exploitable (the CRC still has to match the
key), it violates the principle of strict parsing: accept exactly what you
expect, reject everything else. Loose parsing invites confusion and makes
protocol evolution harder.

### Fix

Change `<` to `!=`:

```cpp
// immo_provisioning.cpp, line 86
if ((size_t)(col1 - key_hex) != 32 || (size_t)(col2 - counter_hex) != 8 || strlen(checksum_hex) != 4) {
```

That's it. One character change: `<` → `!=`.

The `hex_byte` calls on lines 104–108 already validate that the 4 characters
are valid hex digits (returning false on invalid nibbles), so no additional
hex validation loop is needed.

### Why not a bigger change

The original writeup proposed a 15-line replacement with explicit hex character
validation and restructured field-length checks. This is unnecessary because:

1. Field lengths for key (32) and counter (8) are already validated on the same
   line — no change needed.
2. `hex_byte()` (lines 7–18) already rejects non-hex characters by returning
   `false`, which triggers `ERR:MALFORMED` on lines 106–108.
3. The only actual gap is `< 4` vs `!= 4` for the checksum field.

### File note

This fix applies to `ImmoCommon/src/immo_provisioning.cpp`. If Guillemot and
Uguisu have vendored copies under `lib/ImmoCommon/`, the same one-character
change should be mirrored there.

---

## Summary

| Fix | Severity | Change size | Risk |
|-----|----------|-------------|------|
| 1. Reject unprovisioned commands | HIGH | +1 line | None |
| 2. Uguisu persist-before-advertise | HIGH | Reorder 2 lines | Wasted counter on failed advert (harmless) |
| 3. Guillemot persist-before-actuate | MEDIUM | Move + deduplicate `update()` | Wasted counter on failed actuation (harmless) |
| 4. Strict checksum length | MEDIUM | 1 char change | None |
