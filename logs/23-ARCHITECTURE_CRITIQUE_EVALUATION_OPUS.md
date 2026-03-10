# Architecture Critique Evaluation

*Date: 2026-03-10 09:34*

Evaluation of every item from `21-ARCHITECTURE_CRITIQUE_COMBINED.md` against the actual codebase. Each item is rated: **Valid**, **Partially Valid**, **Wrong**, or **Misleading**.

---

## 1. LittleFS Power Drain — Valid, severity overstated

**Claimed problem:** On every button press, Uguisu linearly scans up to 512 CRC32-checked records from a 4KB LittleFS log just to read the last counter. Wastes milliseconds of CPU and flash power before BLE even starts.

**Evaluation:** Confirmed. `immo_storage.cpp:34-44` reads 8-byte records in a while loop. 4096 / 8 = 512 iterations max, each with `read()` + `crc32_ieee()`. However, CRC32 of 4 bytes is microseconds. The full scan is ~2-5ms total — not the power disaster described. The log only reaches 512 records after 512 button presses without rotation.

**Fix:** Add a `last_counter_` cache in a `.noinit` RAM section (survives `sd_power_system_off()`). On cold boot (battery insertion), scan LittleFS once and populate the cache. On subsequent wakes, read from RAM in microseconds.

```cpp
// In immo_storage.h — add retention cache
static uint32_t __attribute__((section(".noinit"))) s_cached_counter;
static uint32_t __attribute__((section(".noinit"))) s_cache_magic;
static constexpr uint32_t CACHE_VALID = 0xC0DE600D;

// In loadLast() — check cache first
uint32_t CounterStore::loadLast() {
  if (s_cache_magic == CACHE_VALID) {
    last_counter_ = s_cached_counter;
  } else {
    load();  // full scan on cold boot only
    s_cache_magic = CACHE_VALID;
  }
  s_cached_counter = last_counter_;
  return last_counter_;
}

// In update() — keep cache in sync
void CounterStore::update(uint32_t counter) {
  // ... existing write logic ...
  s_cached_counter = counter;
}
```

---

## 2. Button Press Timing Offset — Valid

**Claimed problem:** `wait_for_button_press_release()` runs after boot init (~100-200ms). The button press that woke the device is already in progress, so measured duration is shorter than actual. 1000ms long-press threshold effectively requires ~1200ms of physical holding.

**Evaluation:** Confirmed. Boot path before `wait_for_button_press_release()` at `Uguisu/main.cpp:107` includes `delay(50)`, LittleFS init, key load, and `Bluefruit.begin()`. The button is already LOW when the timing starts. Measured press is shorter than physical press by the boot time.

**Fix:** Record boot start time and compensate.

```cpp
// At top of setup()
const uint32_t boot_start = millis();

// When measuring press duration (line 107-110)
const uint32_t press_ms = wait_for_button_press_release(UGUISU_BUTTON_TIMEOUT_MS);
if (press_ms == 0) system_off();
const uint32_t compensated_ms = press_ms + (millis() - boot_start - press_ms);
// Simpler: just add boot overhead directly
const uint32_t total_ms = press_ms + boot_start;  // boot_start ≈ time spent in init
const immo::Command command =
    (total_ms >= UGUISU_LONG_PRESS_MS) ? immo::Command::Lock : immo::Command::Unlock;
```

Alternative simpler fix: just lower the threshold to 800ms to account for typical boot time.

---

## 3. Button Timeout Excessive — Valid, misleading framing

**Claimed problem:** 10 seconds awake at ~5mA waiting for a button press that already triggered the wake. 2-3 seconds is sufficient.

**Evaluation:** The 10s timeout (`uguisu_nrf52840.h:33`) only matters when the button is NOT pressed when code reaches the check (noise wake, or user released during boot). In normal operation, the button is already LOW and the code proceeds immediately to timing the release — the 10s is never reached. The framing suggests energy waste on every press, which doesn't happen.

**Fix:** Reduce to 2-3 seconds. Low-risk change, saves power only on accidental wakes.

```cpp
#define UGUISU_BUTTON_TIMEOUT_MS 2000
```

---

## 4. FreeRTOS LED Task Overkill — Valid, irrelevant in practice

**Claimed problem:** Spawns a FreeRTOS task with 512 bytes of stack just to blink a blue LED during provisioning.

**Evaluation:** Confirmed at `Uguisu/main.cpp:92`. But this only runs when USB-connected for provisioning — never during battery operation. Zero impact on battery life or production use. The 512 bytes is trivial on the nRF52840's 256KB RAM.

**Fix (if desired):** Replace with a FreeRTOS software timer.

```cpp
static TimerHandle_t g_prov_led_timer = nullptr;
static void prov_led_cb(TimerHandle_t) { led::prov_pulse_step(); }

// In setup(), replace xTaskCreate with:
g_prov_led_timer = xTimerCreate("prov", pdMS_TO_TICKS(30), pdTRUE, nullptr, prov_led_cb);
xTimerStart(g_prov_led_timer, 0);

// Cleanup: xTimerDelete(g_prov_led_timer, 0);
```

Requires refactoring `led::prov_pulse()` into a non-blocking step function. Low priority.

---

## 5. Blocking the BLE Stack — Valid, low practical impact

**Claimed problem:** `handle_valid_command()` plays buzzer tones using blocking `delay()` inside `scan_callback`, stalling the BLE scanner for ~260ms.

**Evaluation:** Confirmed. `Guillemot/main.cpp:34-38` uses `delay(duration_ms)`. Two tones (130ms each) + latch pulse (15ms) = ~275ms blocking inside the radio callback. However: the fob sends ~30 duplicate packets over 600ms. Only one needs to arrive before the block. Subsequent duplicates are harmless (counter already accepted). Single-fob system.

**Fix:** Decouple actuation from the BLE callback via a FreeRTOS queue.

```cpp
static QueueHandle_t g_cmd_queue;

// In scan_callback: push payload instead of acting
void scan_callback(ble_gap_evt_adv_report_t* report) {
  immo::Payload pl{};
  if (!parse_payload_from_report(report, pl)) return;
  if (key_is_all_zeros()) return;
  if (!verify_payload(pl)) return;
  xQueueSend(g_cmd_queue, &pl, 0);  // non-blocking
}

// Dedicated task consumes queue
void immo_task(void*) {
  immo::Payload pl;
  for (;;) {
    if (xQueueReceive(g_cmd_queue, &pl, portMAX_DELAY)) {
      handle_valid_command(pl);
    }
  }
}

// In setup():
g_cmd_queue = xQueueCreate(2, sizeof(immo::Payload));
xTaskCreate(immo_task, "immo", 256, nullptr, 1, nullptr);
```

---

## 6. Continuous 5% Duty Cycle — Valid, non-issue

**Claimed problem:** 25ms/500ms scan draws ~250µA average, a constant vampire drain on the scooter battery.

**Evaluation:** Confirmed. But at 250µA on a 15Ah battery: 6mAh/day = 0.04% per day. The scooter would need to sit for ~7 years to drain from scanning alone. Gemini acknowledges this is "acceptable."

**Fix:** Not needed. The current duty cycle is already well-optimized for latency vs. power.

---

## 7. No Watchdog on Guillemot — Valid, CRITICAL

**Claimed problem:** No WDT configured. If firmware hangs (SoftDevice error, flash corruption, radio lockup), the scooter stays in whatever state it was in — potentially unlocked indefinitely.

**Evaluation:** Confirmed. No WDT configuration anywhere in the Guillemot codebase. `loop()` at line 137 is just `sd_app_evt_wait()`. If events stop arriving, the loop sleeps forever. The SR latch holds its last state. This is the most important finding in the entire review.

**Fix:** Configure the nRF52840 WDT. The SoftDevice supports WDT coexistence.

```cpp
#include <nrf_wdt.h>

static nrf_drv_wdt_channel_id g_wdt_channel;

void wdt_event_handler(void) {
  // On WDT timeout: system resets. Latch stays in current state.
  // After reboot, Guillemot re-enters locked scanning mode.
}

// In setup(), after Bluefruit.Scanner.start():
nrf_drv_wdt_config_t config = NRF_DRV_WDT_DEAFULT_CONFIG;
config.reload_value = 8000;  // 8 second timeout
nrf_drv_wdt_init(&config, wdt_event_handler);
nrf_drv_wdt_channel_alloc(&g_wdt_channel);
nrf_drv_wdt_enable();

// In loop() or scan_callback:
nrf_drv_wdt_channel_feed(g_wdt_channel);
```

The WDT must be fed from the main loop or a timer. If `sd_app_evt_wait()` returns events normally, the WDT gets fed. If the system locks up, the WDT resets the MCU after 8 seconds, re-entering the locked scanning state.

---

## 8. `ccm_mic_8()` Return Value Unchecked — WRONG

**Claimed problem:** `verify_payload()` may not check the return value of `ccm_mic_8()`, potentially comparing against an uninitialized buffer.

**Evaluation:** The return value IS checked. `Guillemot/main.cpp:63-64`:

```cpp
if (!immo::ccm_mic_8(g_psk, nonce, msg, sizeof(msg), expected)) return false;
```

The payload is rejected immediately on failure. The reviewer either examined an older version or missed this line.

**Fix:** None needed. Code is already correct.

---

## 9. Nonce Lacks Session Diversifier — Valid, low practical risk

**Claimed problem:** Nonce is `LE32(counter) || 0x00×9`. If the same key is re-provisioned with a counter reset, nonce reuse breaks CCM.

**Evaluation:** Confirmed at `immo_crypto.cpp:35-38`. The 9 zero bytes are wasted. However, Whimbrel generates a new random key each provisioning, so the same (key, nonce) pair only recurs if someone deliberately provisions the same key twice. Low practical risk.

**Fix:** Generate a random 4-byte session ID during provisioning. Store alongside the key. Include in nonce bytes 4-7.

```cpp
// immo_crypto.cpp
void build_nonce(uint32_t counter, const uint8_t session_id[4], uint8_t nonce[NONCE_LEN]) {
  le32_write(&nonce[0], counter);
  memcpy(&nonce[4], session_id, 4);
  for (size_t i = 8; i < NONCE_LEN; i++) nonce[i] = 0;
}
```

Requires adding session_id to the provisioning payload format and storage. Both fob and receiver must agree on it.

---

## 10. Payload Authenticated but Not Encrypted — Valid

**Claimed problem:** Counter and command are sent in plaintext. A passive sniffer sees lock/unlock commands and counter values (usage frequency). Full CCM encryption is nearly free.

**Evaluation:** Confirmed. `Uguisu/main.cpp:128-129` copies plaintext `msg` into the payload. `ccm_mic_8()` only computes the CBC-MAC tag, not the CTR-mode keystream for encryption. A sniffer sees which command was sent and the counter value. However, this doesn't enable attacks — you can't forge a MIC without the key.

**Fix:** Extend `ccm_mic_8()` to also produce the CTR-mode keystream and XOR it with the plaintext. Rename to `ccm_encrypt_8()`.

```cpp
// After computing the MIC (existing code), add CTR encryption:
// Generate keystream blocks A1, A2, ... and XOR with plaintext
for (uint16_t ctr_i = 1; offset_enc < msg_len; ctr_i++) {
  uint8_t ai[16]{};
  ai[0] = static_cast<uint8_t>(L - 1);
  memcpy(&ai[1], nonce, NONCE_LEN);
  ai[14] = static_cast<uint8_t>((ctr_i >> 8) & 0xFF);
  ai[15] = static_cast<uint8_t>(ctr_i & 0xFF);

  uint8_t si[16]{};
  if (!aes128_ecb_encrypt(key, ai, si)) return false;

  const size_t n = min((size_t)16, msg_len - offset_enc);
  for (size_t j = 0; j < n; j++)
    out_ct[offset_enc + j] = msg[offset_enc + j] ^ si[j];
  offset_enc += n;
}
```

Requires corresponding decryption on the receiver side before MIC verification. Cost: one additional AES block (5-byte message fits in one block).

---

## 11. Manual CCM Instead of Hardware NRF_CCM — MISLEADING

**Claimed problem:** The code manually implements CCM via ECB loops. The nRF52840 has a hardware NRF_CCM peripheral that should be used instead.

**Evaluation:** The nRF52840 SoftDevice (S140) **reserves the CCM, AAR, and ECB peripherals for its own use**. Direct access to `NRF_CCM` is blocked. The only crypto API available to application code is `sd_ecb_block_encrypt()`, which is exactly what the code uses. There is no higher-level SoftDevice CCM API for application use. The manual ECB-based CCM implementation is the standard and correct approach on nRF52 with SoftDevice.

**Fix:** None needed. The code is already doing the only thing possible without ditching the SoftDevice.

---

## 12. Counter Log Rotation Power-Failure Bug — Valid, SECURITY BUG

**Claimed problem:** `rotateIfNeeded_()` does `remove(old)` → `rename(current → old)`. If power fails after the rename but before `update()` writes to the new log, `load()` reads the current log (doesn't exist), sets `last_counter_ = 0`, and never reads the old log. Counter resets, enabling replay.

**Evaluation:** Confirmed. `immo_storage.cpp:73-81`: after rotation, `log_path_` doesn't exist. `load()` at line 34-44 only opens `log_path_`. It never reads `old_log_path_`. If `log_path_` doesn't exist, `last_counter_` stays 0. Both Uguisu and Guillemot share this code and are vulnerable.

**Fix:** Scan both files in `load()`, take the maximum.

```cpp
void CounterStore::load() {
  last_counter_ = 0;
  scan_file_(log_path_);
  scan_file_(old_log_path_);
}

void CounterStore::scan_file_(const char* path) {
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(path, Adafruit_LittleFS_Namespace::FILE_O_READ));
  if (!f) return;

  CounterRecord rec{};
  while (f.read(reinterpret_cast<void*>(&rec), sizeof(rec)) == sizeof(rec)) {
    if (record_crc(rec.counter) != rec.crc32) continue;
    if (rec.counter > last_counter_) last_counter_ = rec.counter;
  }
}
```

Add `void scan_file_(const char* path);` to the private section of `CounterStore` in the header.

---

## 13. No Delivery Confirmation — Valid, inherent tradeoff

**Claimed problem:** Fob broadcasts for 600ms and sleeps with no acknowledgment. User gets zero feedback if out of range.

**Evaluation:** Confirmed. The LED on the fob confirms "I sent it," not "they got it." However, adding confirmation requires a connection or back-channel scanner, which would dramatically increase power consumption and complexity. Most car key fobs have the same limitation. This is a deliberate architectural tradeoff, not an oversight.

**Fix:** If desired, add a brief scanner on the fob to listen for an ACK advertisement from Guillemot. Cost: ~50-100ms additional active time per press, plus code on both sides. Probably not worth the complexity for a scooter fob.

---

## 14. Company ID 0xFFFF — Partially valid

**Claimed problem:** 0xFFFF is the BT SIG test/unregistered ID. Other devices using it trigger full MIC checks before rejection.

**Evaluation:** `guillemot_nrf52840.h:14` confirms `MSD_COMPANY_ID = 0xFFFF`. But `Guillemot/main.cpp:128` calls `Bluefruit.Scanner.filterMSD(MSD_COMPANY_ID)`, which filters at the radio level. Only matching advertisements trigger the callback. Random 0xFFFF devices in the wild are rare. For production, a registered ID is proper. For prototyping, fine.

**Fix:** For production, either register a Bluetooth SIG company ID ($7,500/year) or add a magic-byte prefix to the MSD payload for cheaper filtering:

```cpp
// Add 2-byte magic after company ID
static constexpr uint16_t IMMOGEN_MAGIC = 0x494D;  // "IM"

// In parse_payload_from_report: check magic before parsing
if (msd[2] != (IMMOGEN_MAGIC & 0xFF) || msd[3] != (IMMOGEN_MAGIC >> 8)) return false;
```

Increases MSD size by 2 bytes (15 → 17), still within BLE limits.

---

## 15-17. Hardware Documentation Errors — Very likely valid

**Claimed problem:** The technical documentation (`18-CODEBASE_TECHNICAL_DOCUMENTATION.md`) misrepresents the Guillemot PCB in three ways:

1. **MOSFET topology:** Doc shows `Drain ← +BATT, Source → ESC-` (high-side N-FET). An N-FET high-side switch with 42V on the source needs Vg > 46V to turn on. The 10V gate drive can't do this. The actual design must be a low-side switch (Drain ← ESC-, Source → GND, Vgs = 10V).

2. **10V rail purpose:** Doc describes Q3 (P-FET) as a "bleeder" and D2's 10V Zener as being "on the regulated rail." Gemini claims Q3 actually switches +BATT through a Zener to create a 10V gate-drive rail for Q1, with zero quiescent draw when locked.

3. **P-FET gate protection:** Doc doesn't explain how Q3 survives with Source at 42V. Without clamping, Vgs could reach -42V, exceeding the SI2309CDS's -20V max. D1 (12V Zener) must be clamping Vgs, but the doc says D1 "clamps buck input."

**Evaluation:** Cannot verify directly against the KiCad binary schematic, but the electrical arguments are sound. The documented topology is electrically impossible as described. The 10V gate drive only works for a low-side switch.

**Fix:** Rewrite Section 6 of `18-CODEBASE_TECHNICAL_DOCUMENTATION.md`. The signal flow diagram should show:

```
Q1 (IPB042N10N3G) — LOW-SIDE SWITCH:
  Drain ← ESC- (load return path)
  Source → GND (battery negative)
  Gate  ← ISO_GATE (10V when unlocked, 0V when locked)

Q3 (SI2309CDS) — 10V GATE DRIVE SWITCH:
  Source ← +BATT
  Drain  → R5 + D2 (10V Zener) → 10V_RAIL → Q1 gate
  Gate   ← D1 (12V Zener clamp) ← Q4 (2N7002) ← Q_UNLOCK
  Purpose: Switches battery through Zener regulator to create 10V gate drive.
           Zero quiescent current when locked (Q3 off, no current path).

D1 (BZT52C12-7-F) — GATE PROTECTION:
  Clamps Q3 Vgs to -12V (within SI2309CDS -20V abs max).
  NOT for "clamping buck input."
```

Verify these connections against the actual KiCad schematic before committing.

---

## 18. Hardware Design Issues — Valid, production hardening

**Claimed problem:** Five hardware issues identified: no reverse polarity protection, TVS overshoot, dual-NPN buzzer driver, no ESD protection, no conformal coating.

**Evaluation:** All are legitimate engineering concerns for a production design. None are showstoppers for a prototype.

**Fixes:**

- **Reverse polarity:** Add a P-FET ideal diode or Schottky on +BATT input. Schottky (e.g., SS54) is simplest; P-FET (e.g., SI2301) gives lower drop.
- **TVS overshoot:** Verify D1 Zener can handle the transient current during a TVS clamp event. If not, upsize D1 or add a second TVS stage.
- **Buzzer driver:** Replace high-side Q5 (NPN) with a PNP (e.g., MMBT3906). Or simplify to a single low-side N-FET.
- **ESD protection:** Add PESD5V0S2BT or similar on D0, D1, D3 lines.
- **Conformal coating:** Specify in production documentation. MG Chemicals 419D or similar.

All are schematic/BOM changes. No firmware modifications needed.

---

## 19. Extremely Short Button Presses (< 200ms) — Valid, missed UX edge case

**Claimed problem (User):** What happens if the user presses the button too short (shorter than 200ms)? The device wakes up but might miss the press. Can we make it unlock in that scenario?

**Evaluation:** Very valid. When the fob wakes from `system_off`, the boot process takes ~100-200ms. If the user releases the button before `wait_for_button_press_release()` starts, the pin is already HIGH. The function's first `while` loop will then sit there for 10 seconds waiting for a *new* press, eventually timing out and sleeping. The quick press is completely ignored.

**Fix:** Since the only wake source from `system_off` is the button, we *know* a press occurred if we just booted. If the button is already HIGH when we check it, we can immediately return a small duration (e.g., 1ms) to trigger an immediate Unlock, rather than waiting 10 seconds.

```cpp
// In wait_for_button_press_release(), after initial boot:
if (digitalRead(UGUISU_PIN_BUTTON) != LOW) {
  // Pin already HIGH. We woke from sleep but user already released.
  // Treat as a very short press (Unlock).
  return 1; 
}
```

---

## 20. Early Exit on Long Press (> 1s) — Valid, UX improvement

**Claimed problem (User):** What happens if the user presses the button too long? Can we just set a timeout once it hits 1 sec and proceed to locking without waiting for release?

**Evaluation:** Valid. Currently, `wait_for_button_press_release()` blocks until the user *releases* the button, up to the 10-second timeout. If the user holds the button for 3 seconds, they get zero LED feedback until they let go. Waiting for release is unnecessary once the 1000ms `UGUISU_LONG_PRESS_MS` threshold is crossed.

**Fix:** Break out of the polling loop immediately once the duration exceeds the long-press threshold. This provides instant LED feedback exactly at 1 second, making the fob feel much more responsive.

```cpp
const uint32_t press_start = millis();
while (millis() < deadline && digitalRead(UGUISU_PIN_BUTTON) == LOW) {
  if (millis() - press_start >= UGUISU_LONG_PRESS_MS) {
    // Threshold reached, no need to wait for release
    return UGUISU_LONG_PRESS_MS; 
  }
  delay(10);
}
return millis() - press_start;
```

---

## Priority Summary

| Priority | Item | Action |
|----------|------|--------|
| **P0** | #7 No watchdog | Add WDT to Guillemot immediately |
| **P0** | #12 Rotation power-failure bug | Fix `load()` to scan both files |
| **P1** | #15-17 Hardware doc errors | Rewrite Section 6 of technical docs |
| **P2** | #10 No encryption | Add CTR-mode encryption to CCM |
| **P2** | #2 Button timing offset | Compensate or lower threshold |
| **P2** | #18 Hardware hardening | Address for production revision |
| **P2** | #19 Short press missed | Fix `wait_for_button_press_release` to assume Unlock if already HIGH |
| **P2** | #20 Long press delayed UX | Early exit from polling loop at 1000ms |
| **P3** | #1 LittleFS cache | Add noinit RAM cache |
| **P3** | #5 BLE blocking | Decouple with FreeRTOS queue |
| **P3** | #9 Nonce diversifier | Add session ID to provisioning |
| **P3** | #14 Company ID | Add magic prefix or register ID |
| Skip | #3 Button timeout | Trivial config change if desired |
| Skip | #4 FreeRTOS LED task | No real-world impact |
| Skip | #6 Duty cycle | Non-issue on scooter battery |
| N/A | #8 Return value check | Already correct, no action |
| N/A | #11 Hardware CCM | Not possible with SoftDevice |
| N/A | #13 Delivery confirmation | Architectural tradeoff, not a bug |
