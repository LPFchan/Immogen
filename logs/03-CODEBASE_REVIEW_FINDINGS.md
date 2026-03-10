# BLE Immobilizer — Technical Review Findings

*Date: 2026-03-09 06:16*

*Items requiring attention. Items executed well are omitted.*

---

## Architecture Concerns

### BLE Scan/Advert Timing
- Guillemot: `SCAN_INTERVAL_MS = 2000`, `SCAN_WINDOW_MS = 20` (1% duty cycle)
- Uguisu: adverts at 100 ms intervals for 2 s
- If Guillemot's scan window falls between adverts, it can miss packets entirely
- Worst-case latency can reach several seconds; document this behavior

### Counter Persistence Order (Uguisu)
- `g_store.update(counter)` runs *after* `delay(UGUISU_ADVERTISE_MS)`
- Power loss during that window can cause counter reuse on next boot
- **Recommendation:** Persist counter before or at start of advertising

### Unprovisioned Receiver (Guillemot)
- `key_is_all_zeros()` used only for provisioning, not for rejecting commands
- Unprovisioned receiver can still process adverts; attacker with zero key could craft valid MIC
- **Recommendation:** Add `if (key_is_all_zeros()) return;` in scan callback before handling commands

---

## ImmoCommon: immo_provisioning

### Checksum Validation
- Line 86: `strlen(checksum_hex) < 4` — CRC-16 yields 4 hex chars
- Trailing garbage after checksum could pass validation; format not strictly enforced
- **Recommendation:** Require exactly 4 hex chars or validate trailing content

### ensure_provisioned Loop
- When VBUS present and not provisioned, loops indefinitely
- Can block boot when USB plugged but no host connected
- **Recommendation:** Add timeout or "give up after N attempts" for non-USB use

---

## ImmoCommon: immo_storage

### load() and Partial Records
- Truncated write could leave half-record; `read` returning partial bytes may misalign subsequent reads
- **Recommendation:** Add handling for partial reads or detect/recover from misalignment

### rotateIfNeeded_
- Verify Adafruit LittleFS behavior when log file doesn't exist on first run; confirm `f.size()` is safe

---

## Guillemot: main.cpp

### Lock Order and Counter Update
- Lock: buzzer → latch RESET → `g_store.update(counter)`
- If `update()` fails (FS, power loss), latch is already reset but counter not persisted; next unlock may be rejected as replay
- **Recommendation:** Update counter before or with latch; document failure behavior

---

## Whimbrel: serial.js

### readLineWithTimeout Concurrency
- Multiple concurrent `readLineWithTimeout` calls resolve in FIFO order; lines could be routed to wrong consumer
- Current provisioning flow is sequential, so OK in practice
- **Note:** Document that concurrent use is not supported

---

## Whimbrel: dfu.js

### Response Matching
- Responses matched by opcode only; some Nordic DFU ops emit multiple responses (e.g. PRN)
- Out-of-order or duplicate responses could misroute
- Acceptable for standard flows; fragile for edge cases

---

## Summary

| Priority | Item | Location |
|----------|------|----------|
| High | Reject commands when unprovisioned | Guillemot scan_callback |
| High | Persist counter before advertising | Uguisu setup |
| Medium | Lock: update counter before/with latch | Guillemot handle_valid_command |
| Medium | Stricter checksum validation | immo_provisioning |
| Medium | Document BLE scan/advert timing | Docs |
| Low | ensure_provisioned timeout option | immo_provisioning |
| Low | load() partial-record handling | immo_storage |
