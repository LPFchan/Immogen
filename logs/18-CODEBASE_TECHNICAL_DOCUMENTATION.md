# Immogen Codebase Technical Documentation

*Date: 2026-03-10 08:38*

Immogen is a three-part BLE immobilizer system for the Ninebot Max G30 electric scooter. It consists of **Uguisu** (key fob), **Guillemot** (deck receiver), and a shared cryptographic library. A companion web app, **Whimbrel** (separate repo), handles key provisioning via Web Serial.

---

## Table of Contents

1. [Project Structure](#1-project-structure)
2. [Shared Library — `lib/`](#2-shared-library--lib)
   - [ImmoCommon.h](#immocommonh)
   - [immo_crypto](#immo_crypto)
   - [immo_provisioning](#immo_provisioning)
   - [immo_storage](#immo_storage)
   - [immo_util](#immo_util)
   - [immo_tusb_config.h](#immo_tusb_configh)
   - [library.json](#libraryjson)
3. [Guillemot — Deck Receiver](#3-guillemot--deck-receiver)
   - [guillemot_config.h](#guillemot_configh)
   - [guillemot_nrf52840.h](#guillemot_nrf52840h)
   - [main.cpp](#guillemot-maincpp)
4. [Uguisu — Key Fob](#4-uguisu--key-fob)
   - [uguisu_config.h](#uguisu_configh)
   - [uguisu_nrf52840.h](#uguisu_nrf52840h)
   - [led_effects.h](#led_effectsh)
   - [main.cpp](#uguisu-maincpp)
5. [BLE Protocol](#5-ble-protocol)
6. [Hardware Design — Guillemot PCB](#6-hardware-design--guillemot-pcb)
   - [KiCad Project](#guillemot-kicad-project)
   - [Schematic Analysis](#guillemot-schematic-analysis)
   - [PCB Layout](#guillemot-pcb-layout)
   - [Bill of Materials](#guillemot-bill-of-materials)
   - [Production Files](#guillemot-production-files)
7. [Hardware Design — Uguisu PCB](#7-hardware-design--uguisu-pcb)
   - [KiCad Project](#uguisu-kicad-project)
   - [Schematic Analysis](#uguisu-schematic-analysis)
   - [PCB Layout](#uguisu-pcb-layout)
   - [Bill of Materials](#uguisu-bill-of-materials)
8. [Build System](#8-build-system)
9. [CI/CD Pipeline](#9-cicd-pipeline)
10. [Tools](#10-tools)
11. [Module Dependency Graph](#11-module-dependency-graph)

---

## 1. Project Structure

```
Immogen/
├── lib/                           # Shared C++ library (ImmoCommon)
│   ├── ImmoCommon.h               # Header aggregator
│   ├── immo_crypto.h/.cpp         # AES-128-CCM MIC operations
│   ├── immo_provisioning.h/.cpp   # USB serial key provisioning
│   ├── immo_storage.h/.cpp        # Counter log (LittleFS)
│   ├── immo_util.h/.cpp           # Key validation, fatal error loop
│   ├── immo_tusb_config.h         # TinyUSB MSC disable
│   └── library.json               # PlatformIO library metadata
├── Guillemot/                     # Deck receiver
│   ├── firmware/
│   │   ├── src/main.cpp           # BLE scanner + latch/buzzer control
│   │   ├── include/
│   │   │   ├── guillemot_config.h
│   │   │   └── guillemot_nrf52840.h
│   │   └── platformio.ini
│   ├── hardware/                  # KiCad schematic, PCB, production files
│   ├── logs/                      # Hardware design docs
│   └── README.md
├── Uguisu/                        # Key fob
│   ├── firmware/
│   │   ├── src/main.cpp           # Button → encrypt → advertise → sleep
│   │   ├── include/
│   │   │   ├── uguisu_config.h
│   │   │   ├── uguisu_nrf52840.h
│   │   │   └── led_effects.h
│   │   └── platformio.ini
│   ├── hardware/                  # KiCad schematic, PCB, production files
│   └── README.md
├── tools/
│   ├── test_vectors/gen_mic.py    # Python MIC test-vector generator
│   ├── led_visualizer.html        # LED timing tuner
│   ├── buzzer_tuner.html          # Buzzer frequency tuner
│   └── ble_timing_simulator.html  # BLE scan/adv timing sim
├── logs/                          # Technical writeups
├── .github/workflows/release.yml  # CI/CD
├── .gitignore
└── README.md
```

---

## 2. Shared Library — `lib/`

All firmware modules link against `ImmoCommon` via PlatformIO symlink. Every source file lives under the `immo` namespace.

### ImmoCommon.h

Header aggregator — a single `#include <ImmoCommon.h>` pulls in all library modules:

```cpp
#include "immo_crypto.h"
#include "immo_provisioning.h"
#include "immo_storage.h"
#include "immo_util.h"
```

---

### immo_crypto

**Files:** `immo_crypto.h` (30 lines), `immo_crypto.cpp` (93 lines)

Provides AES-128-CCM message authentication code generation and constant-time comparison.

#### Constants

| Name | Value | Meaning |
|------|-------|---------|
| `MIC_LEN` | 8 | Authentication tag length (bytes) |
| `MSG_LEN` | 5 | Plaintext: counter(4) + command(1) |
| `PAYLOAD_LEN` | 13 | msg(5) + mic(8) — the BLE payload |
| `NONCE_LEN` | 13 | le32(counter) + 9 zero bytes |

#### Enum: `Command`

```cpp
enum class Command : uint8_t {
  Unlock = 0x01,
  Lock   = 0x02,
};
```

#### Struct: `Payload`

```cpp
struct Payload {
  uint32_t counter;       // Anti-replay monotonic counter
  Command  command;       // Unlock or Lock
  uint8_t  mic[MIC_LEN]; // 8-byte AES-CCM tag
};
```

#### Internal (anonymous namespace)

| Function | Signature | Purpose |
|----------|-----------|---------|
| `le32_write` | `(uint8_t out[4], uint32_t x)` | Writes a 32-bit value in little-endian byte order. |
| `aes128_ecb_encrypt` | `(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) → bool` | Single-block AES-128 ECB via nRF SoftDevice `sd_ecb_block_encrypt()`. Returns false on SoftDevice error. |
| `xor_block` | `(uint8_t dst[16], const uint8_t a[16], const uint8_t b[16])` | XORs two 16-byte blocks into `dst`. |

#### Public Functions

**`build_nonce(uint32_t counter, uint8_t nonce[NONCE_LEN])`** *(line 35)*
Constructs a 13-byte CCM nonce: 4 bytes LE counter followed by 9 zero bytes.

**`build_msg(uint32_t counter, Command command, uint8_t msg[MSG_LEN])`** *(line 40)*
Builds the 5-byte plaintext message: 4 bytes LE counter + 1 byte command.

**`ccm_mic_8(key, nonce, msg, msg_len, out_mic) → bool`** *(line 45)*
Computes an 8-byte AES-128-CCM authentication tag. Implementation:
1. Constructs the B0 formatting block: `flags_b0 = ((M-2)/2) << 3 | (L-1)` where M=8 (tag length), L=2 (length-field size). B0 = `[flags | nonce(13) | msg_len(2 BE)]`.
2. CBC-MAC pass: XORs B0 with zero block, encrypts; then processes message in 16-byte blocks (zero-padded tail).
3. Constructs counter block A0: `[L-1 | nonce(13) | 0x0000]`, encrypts to get keystream S0.
4. Final tag: `out_mic[i] = CBC_MAC[i] ^ S0[i]` for i in [0..7].

Returns false if any `sd_ecb_block_encrypt()` call fails (SoftDevice unavailable).

**`constant_time_eq(a, b, n) → bool`** *(line 86)*
Accumulates XOR differences across all `n` bytes, returns true only if no difference. Prevents timing side-channel attacks on MIC verification.

---

### immo_provisioning

**Files:** `immo_provisioning.h` (51 lines), `immo_provisioning.cpp` (199 lines)

Handles key provisioning from the Whimbrel web app over USB serial.

#### Constants

| Name | Value | Meaning |
|------|-------|---------|
| `PROV_MAGIC` | `0x564F5250` | "PROV" in memory on LE ARM — magic prefix for flash key file |
| `DEFAULT_PROV_PATH` | `"/prov.bin"` | LittleFS path for the provisioned key |
| `DEFAULT_PROV_TIMEOUT_MS` | `30000` | Serial listen timeout (30 s) |

#### Internal (anonymous namespace)

**`hex_byte(const char* hex, uint8_t* out) → bool`** *(line 10)*
Parses two hex ASCII characters into one byte. Supports 0-9, a-f, A-F.

**`parse_counter_hex(const char* hex, uint32_t* out) → bool`** *(line 24)*
Parses 8 hex characters into a big-endian uint32_t.

**`crc16_ccitt(const uint8_t* data, size_t len) → uint16_t`** *(line 38)*
CRC-16-CCITT: polynomial 0x1021, initial value 0xFFFF. Used to validate the provisioning checksum.

#### Public Functions

**`prov_is_vbus_present() → bool`** *(line 50)*
Checks `NRF_POWER->USBREGSTATUS` VBUS detect bit. Returns true when USB is physically connected. Compiles to `return false` on non-USB nRF52 variants.

**`prov_run_serial_loop(timeout_ms, on_success) → bool`** *(line 58)*
Blocking serial listener. Reads characters into a 128-byte line buffer until newline.

**Protocol format:** `PROV:<key_32_hex>:<counter_8_hex>:<checksum_4_hex>\n`

Parsing sequence:
1. Validates `PROV:` prefix and two colon separators
2. Checks field lengths: key=32 hex, counter=8 hex, checksum=4 hex
3. Decodes 16-byte key via `hex_byte()`
4. Decodes counter via `parse_counter_hex()`
5. Decodes 2-byte checksum and verifies against `crc16_ccitt(key, 16)`
6. Calls `on_success(key, counter)` — if it returns true, prints `ACK:PROV_SUCCESS`

Serial error responses: `ERR:MALFORMED`, `ERR:CHECKSUM`, `ERR:STORAGE`.

Timeout only enforced before first byte arrives; once data starts streaming, the line is read to completion.

**`ensure_provisioned(timeout_ms, on_success, load_provisioning, is_provisioned)`** *(line 133)*
High-level provisioning orchestrator:
1. If VBUS present: runs one serial loop (allows re-provisioning even if already provisioned), then calls `load_provisioning()`.
2. If still not provisioned and VBUS still present: loops indefinitely calling `prov_run_serial_loop()` + `load_provisioning()` until either provisioning succeeds or USB is disconnected.

**`prov_write_and_verify(path, key, counter, store, runtime_key) → bool`** *(line 149)*
Writes 20 bytes to flash: `PROV_MAGIC(4) + key(16)`. Then reads back and verifies with `constant_time_eq()`. On success, seeds the counter store and copies key into runtime buffer. Used as the `on_success` callback.

**`prov_load_key(path, out_key) → bool`** *(line 183)*
Opens the provisioning file, verifies magic prefix, reads 16-byte key. Returns false if file missing, too small, or wrong magic.

**`prov_load_key_or_zero(path, out_key)`** *(line 194)*
Wrapper: loads key or `memset(out_key, 0, 16)` if not provisioned.

---

### immo_storage

**Files:** `immo_storage.h` (49 lines), `immo_storage.cpp` (85 lines)

Append-only counter log for anti-replay protection, stored on nRF52840 internal flash via LittleFS.

#### Constants

| Name | Value | Meaning |
|------|-------|---------|
| `DEFAULT_COUNTER_LOG_MAX_BYTES` | 4096 | Log rotation trigger size |

#### Struct: `CounterRecord`

```cpp
struct CounterRecord {
  uint32_t counter;  // The counter value
  uint32_t crc32;    // IEEE CRC-32 of counter bytes
};
```

8 bytes per record. At 4096 bytes max, the log holds up to 512 records before rotation.

#### Internal (anonymous namespace)

**`crc32_ieee(data, len) → uint32_t`** *(line 8)*
Standard IEEE CRC-32: polynomial 0xEDB88320 (reflected), initial 0xFFFFFFFF, final invert. Branchless via arithmetic mask `-(crc & 1u)`.

**`record_crc(counter) → uint32_t`** *(line 20)*
Computes CRC-32 over the 4 bytes of a counter value.

#### Class: `CounterStore`

**Constructor:** `CounterStore(log_path, old_log_path, max_bytes)`
Stores file paths and rotation threshold. Initializes `last_counter_` to 0.

**`begin() → bool`** *(line 30)*
Initializes `InternalFS` (LittleFS on nRF52840 internal flash). Returns false on failure (treated as fatal by both firmware projects).

**`load()`** *(line 34)*
Scans the log file sequentially, reading `CounterRecord` structs. Skips records with invalid CRC-32. Tracks the maximum valid counter seen. Resets `last_counter_` to 0 before scanning (so an empty log yields 0).

**`lastCounter() → uint32_t`** *(line 47)*
Returns the cached `last_counter_` value.

**`loadLast() → uint32_t`** *(line 27, header inline)*
Calls `load()` then returns `last_counter_`. Used by Uguisu to get the counter before incrementing.

**`update(uint32_t counter)`** *(line 51)*
Calls `rotateIfNeeded_()`, then appends a new `CounterRecord` to the log. Opens with `FILE_O_WRITE` which seeks to end in Adafruit LittleFS (append semantics). Updates cached `last_counter_`.

**`seed(uint32_t counter)`** *(line 67)*
Deletes both log and old_log files, then calls `update()` to write a single initial record. Used during provisioning to reset the counter state.

**`rotateIfNeeded_()`** *(line 73, private)*
If the current log file size >= `max_bytes_`, deletes the old log file and renames the current log to the old path. The next `update()` call recreates the current log from scratch.

---

### immo_util

**Files:** `immo_util.h` (13 lines), `immo_util.cpp` (26 lines)

#### Functions

**`is_key_blank(const uint8_t key[16]) → bool`** *(line 6)*
Returns true if all 16 bytes are zero. Used to check whether a device has been provisioned.

**`led_error_loop(int led_pin)`** *(line 12)*
`[[noreturn]]` Fatal error handler. If `led_pin >= 0`, toggles the pin at 200 ms on / 200 ms off indefinitely. If negative, spins with `delay(1000)`. Used by Guillemot for InternalFS init failure.

---

### immo_tusb_config.h

*(17 lines)*

Overrides the default Adafruit nRF52 TinyUSB configuration:
- Includes `arduino/ports/nrf/tusb_config_nrf.h` (default config)
- Undefines and redefines `CFG_TUD_MSC` and `CFG_TUH_MSC` to 0

This disables the USB Mass Storage Class to prevent a `File` class name collision between SdFat (pulled in by TinyUSB MSC) and Adafruit_LittleFS.

---

### library.json

PlatformIO library descriptor:
- **Name:** ImmoCommon
- **Version:** 0.1.0
- **Frameworks:** arduino
- **Platforms:** all (`*`)

Both firmware projects include it via `ImmoCommon=symlink://../../lib`.

---

## 3. Guillemot — Deck Receiver

Guillemot is installed on the scooter deck. It continuously scans for BLE advertisements from Uguisu, verifies them cryptographically, and controls a latching power gate via an SR latch.

### guillemot_config.h

*(7 lines)*

Config indirection layer: optionally includes `guillemot_config_local.h` if present (for per-device overrides), then includes `guillemot_nrf52840.h`.

### guillemot_nrf52840.h

*(44 lines)*

Hardware constants and inline helpers for the XIAO nRF52840 target.

#### Pin Assignments

| Constant | Value | Function |
|----------|-------|----------|
| `PIN_LATCH_SET` | `D0` | SR latch SET input |
| `PIN_LATCH_RESET` | `D1` | SR latch RESET input |
| `PIN_BUZZER` | `D3` | Piezo buzzer (PWM tone) |
| `PIN_ERROR_LED` | `26` | Onboard red LED (P0.26) |

#### BLE Scan Parameters

| Constant | Value | Meaning |
|----------|-------|---------|
| `SCAN_INTERVAL_MS` | 500 | Time between scan windows |
| `SCAN_WINDOW_MS` | 25 | Active scan duration per interval |
| `MSD_COMPANY_ID` | `0xFFFF` | BLE Manufacturer Specific Data company ID |

Duty cycle: 25/500 = 5%. Since the scan window (25 ms) exceeds the Uguisu advertisement interval (20 ms), detection probability is 100% during any burst.

#### Buzzer Tones

| Constant | Value |
|----------|-------|
| `BUZZER_LOW_HZ` | 2637 (E7) |
| `BUZZER_HIGH_HZ` | 3952 (B7) |
| `BUZZER_LOW_MS` | 130 |
| `BUZZER_HIGH_MS` | 130 |

#### Latch Control

`LATCH_PULSE_MS = 15` — pulse width for SET/RESET.

**`latch_set_pulse()`** *(line 32)*
Ensures RESET is LOW, pulses SET HIGH for 15 ms, then LOW. Latches the power gate open (unlock).

**`latch_reset_pulse()`** *(line 39)*
Ensures SET is LOW, pulses RESET HIGH for 15 ms, then LOW. Latches the power gate closed (lock).

---

### Guillemot main.cpp

*(139 lines)*

#### File-Scope State (anonymous namespace)

| Variable | Type | Purpose |
|----------|------|---------|
| `COUNTER_LOG_PATH` | `const char*` | `"/ctr.log"` |
| `OLD_COUNTER_LOG_PATH` | `const char*` | `"/ctr.old"` |
| `g_psk[16]` | `uint8_t` | Runtime pre-shared key |
| `g_store` | `CounterStore` | Anti-replay counter log |

#### Internal Functions

**`on_provision_success(key, counter) → bool`** *(line 24)*
Provisioning callback: delegates to `prov_write_and_verify()` with `g_psk` as the runtime key buffer.

**`key_is_all_zeros() → bool`** *(line 28)*
Wraps `immo::is_key_blank(g_psk)`. Used as the `is_provisioned` predicate (inverted sense: returns true = NOT provisioned).

**`load_psk_from_storage()`** *(line 30)*
Loads PSK from flash into `g_psk`, zeroing it if not found.

**`buzzer_tone_ms(hz, duration_ms)`** *(line 34)*
Plays a tone on the buzzer pin using Arduino `tone()`, blocks for the duration, then silences with `noTone()`.

**`parse_payload_from_report(report, out) → bool`** *(line 40)*
Extracts manufacturer-specific data from a BLE advertisement report:
1. Calls `Bluefruit.Scanner.parseReportByType()` for `BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA`
2. Verifies total length is exactly 15 bytes (2 company ID + 13 payload)
3. Checks company ID matches `MSD_COMPANY_ID`
4. Decodes counter (4 bytes LE), command (1 byte), MIC (8 bytes) into the `Payload` struct

**`verify_payload(pl) → bool`** *(line 56)*
Verifies the MIC:
1. Builds nonce from `pl.counter`
2. Builds message from `pl.counter` + `pl.command`
3. Computes expected MIC via `ccm_mic_8()` using `g_psk`
4. Compares expected vs received MIC using `constant_time_eq()`

**`handle_valid_command(pl)`** *(line 68)*
Processes a cryptographically valid payload:
1. **Anti-replay:** Rejects if `pl.counter <= g_store.lastCounter()`
2. Updates counter log with `g_store.update(pl.counter)`
3. **Unlock:** `latch_set_pulse()` then ascending two-tone buzzer (LOW → HIGH)
4. **Lock:** Descending two-tone buzzer (HIGH → LOW) then `latch_reset_pulse()`

**`scan_callback(report)`** *(line 91)*
BLE scan callback registered with Bluefruit. Pipeline: parse → check key exists → verify MIC → handle command. Silent no-op on any failure.

#### `setup()` *(line 101)*

1. Configure GPIO: latch pins LOW, buzzer off
2. `Serial.begin(115200)`, 50 ms settle delay
3. `g_store.begin()` — fatal `led_error_loop(PIN_ERROR_LED)` on failure
4. `load_psk_from_storage()` — load key from flash
5. `ensure_provisioned()` — if USB connected, listen for provisioning
6. `g_store.load()` — scan counter log, cache highest counter
7. `Bluefruit.begin(0, 1)` — 0 peripheral connections, 1 central
8. Set device name to "Guillemot"
9. Configure scanner: passive scan, MSD company ID filter, 500/25 ms interval/window
10. `Bluefruit.Scanner.start(0)` — scan indefinitely
11. Print `BOOTED:Guillemot`

#### `loop()` *(line 136)*

Calls `sd_app_evt_wait()` — yields CPU to SoftDevice, wakes on BLE events. All work happens in `scan_callback()`.

---

## 4. Uguisu — Key Fob

Uguisu is a battery-powered BLE fob. On button press it encrypts a command, broadcasts it as a BLE advertisement, shows LED feedback, then enters deep sleep.

### uguisu_config.h

*(8 lines)*

Config indirection: optionally includes `uguisu_config_local.h`, then `uguisu_nrf52840.h`.

### uguisu_nrf52840.h

*(76 lines)*

Hardware constants and inline functions for the XIAO nRF52840 fob board.

#### Pin Assignments

| Constant | Value | Function |
|----------|-------|----------|
| `UGUISU_PIN_BUTTON` | `D0` | Tactile button (active-low) |
| `UGUISU_PIN_BUTTON_NRF` | `2` | nRF P0.02 (for system-off wake) |
| `PIN_LED_B` | `D1` | RGB LED blue channel (active-low) |
| `PIN_LED_R` | `D2` | RGB LED red channel (active-low) |
| `PIN_LED_G` | `D3` | RGB LED green channel (active-low) |
| `PIN_ERROR_LED` | `26` | Onboard red LED (P0.26) |

#### Timing Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `UGUISU_ADVERTISE_MS` | 600 | Total broadcast duration |
| `UGUISU_ADV_INTERVAL_MS` | 20 | Min BLE advertisement interval |
| `UGUISU_LONG_PRESS_MS` | 1000 | Threshold: >= 1s = Lock, else Unlock |
| `UGUISU_BUTTON_TIMEOUT_MS` | 10000 | Max wait for button before sleep |
| `LED_LOWBAT_MV_THRESHOLD` | 3400 | Low-battery voltage threshold (mV) |

#### Inline Functions

**`readVbat_mv() → uint16_t`** *(line 51)*
Reads battery voltage via ADC:
1. Enables the VBAT PMOS switch if available (`VBAT_ENABLE` pin)
2. Configures 3.0V internal reference, 12-bit resolution
3. Reads `UGUISU_VBAT_PIN` (P0.31 / AIN7)
4. Formula: `raw_12bit * 6000 / 4096` (accounts for 1M/1M voltage divider, 2x compensation)
5. Disables PMOS switch to eliminate divider current draw

**`system_off()`** *(line 68)*
Deep sleep entry:
1. Stops BLE advertising
2. 10 ms settle delay
3. Configures `UGUISU_PIN_BUTTON_NRF` (P0.02) as sense input with pullup, low-level wake detect
4. Calls `sd_power_system_off()` — enters nRF52 system-off mode (~0.3 uA)

---

### led_effects.h

*(174 lines)*

Complete LED animation system for the RGB LED (XL-5050RGBC). All LEDs are active-low: PWM 0 = full on, 255 = off. The `led` namespace inverts this so API callers use brightness 0-255 normally.

#### Timing Constants

**Unlock / Lock single flash:**

| Constant | Default | Purpose |
|----------|---------|---------|
| `LED_FLASH_RISE_MS` | 300 | Fade-in duration |
| `LED_FLASH_HOLD_MS` | 150 | Hold at full brightness |
| `LED_FLASH_FALL_MS` | 300 | Fade-out duration |

**Provisioning blue pulse:**

| Constant | Default | Purpose |
|----------|---------|---------|
| `LED_PROV_RISE_MS` | 500 | Fade-in |
| `LED_PROV_HOLD_MS` | 200 | Hold |
| `LED_PROV_FALL_MS` | 500 | Fade-out |
| `LED_PROV_OFF_MS` | 50 | Off gap between pulses |

**Low-battery rapid pulse:**

| Constant | Default | Purpose |
|----------|---------|---------|
| `LED_LOWBAT_RISE_MS` | 120 | Fade-in |
| `LED_LOWBAT_HOLD_MS` | 100 | Hold |
| `LED_LOWBAT_FALL_MS` | 120 | Fade-out |
| `LED_LOWBAT_OFF_MS` | 170 | Off gap |
| `LED_LOWBAT_WINDOW_MS` | 2000 | Total blink window |

#### Internal

**`_write(pin, brightness)`** *(line 59)*
Inverts brightness for active-low drive: `analogWrite(pin, 255 - brightness)`.

#### Functions

**`init()`** *(line 64)*
Sets R/G/B pins as OUTPUT, all off.

**`off()`** *(line 71)*
Turns all three LED channels off.

**`fade_in(pin, duration_ms)`** *(line 78)*
64-step linear ramp from 0 to 255 brightness. Blocking.

**`fade_out(pin, duration_ms)`** *(line 88)*
64-step linear ramp from 255 to 0 brightness. Blocking.

**`flash_once(pin, rise_ms, hold_ms, fall_ms)`** *(line 98)*
Single flash: fade in → hold → fade out → off.

**`pulse_once(pin, rise_ms, hold_ms, fall_ms, off_ms)`** *(line 107)*
One pulse cycle with off gap at end. Designed for infinite loop in a FreeRTOS task.

**`pulse_blink(pin, rise_ms, hold_ms, fall_ms, off_ms, total_ms)`** *(line 118)*
Rapid pulsed blink for a total duration. Checks deadline before each phase.

**`error_loop(onboard_pin)`** *(line 135)*
`[[noreturn]]` Fatal: rapid 150 ms red blink on both RGB LED and onboard LED. Self-contained (safe before `init()`).

#### Convenience Wrappers

| Function | Color | Behavior |
|----------|-------|----------|
| `flash_unlock()` | Green | Single flash (300/150/300 ms) |
| `flash_lock()` | Red | Single flash (300/150/300 ms) |
| `flash_low_battery(pin)` | Caller's choice | Rapid pulse for 2 s |
| `prov_pulse()` | Blue | Single pulse (500/200/500/50 ms) |

---

### Uguisu main.cpp

*(150 lines)*

#### File-Scope State (anonymous namespace)

| Variable | Type | Purpose |
|----------|------|---------|
| `COUNTER_LOG_PATH` | `const char*` | `"/ug_ctr.log"` |
| `OLD_COUNTER_LOG_PATH` | `const char*` | `"/ug_ctr.old"` |
| `g_psk[16]` | `uint8_t` | Runtime pre-shared key |
| `g_store` | `CounterStore` | Counter log (separate from Guillemot) |
| `g_prov_led_task` | `TaskHandle_t` | FreeRTOS handle for provisioning LED task |

#### Internal Functions

**`on_provision_success(key, counter) → bool`** *(line 25)*
Same pattern as Guillemot: delegates to `prov_write_and_verify()`.

**`key_is_all_zeros() → bool`** *(line 29)*
Checks if g_psk is blank.

**`load_provisioning()`** *(line 31)*
Loads PSK from flash into `g_psk`.

**`prov_led_task(void*)`** *(line 36)*
FreeRTOS task body: infinite loop calling `led::prov_pulse()` (blue breathing LED during provisioning).

**`start_advertising_once(company_id, payload13)`** *(line 40)*
Configures and starts a single BLE advertisement burst:
1. Stops any current advertising, clears data
2. Adds standard flags (`LE_ONLY_GENERAL_DISC_MODE`), TX power, device name
3. Adds 15-byte MSD: `company_id(2 LE) + payload(13)`
4. Sets interval: converts ms to BLE 0.625 ms units via `(ms * 8 + 4) / 5`
5. Disables reconnect-on-disconnect
6. Starts advertising indefinitely (stopped explicitly later)

**`wait_for_button_press_release(timeout_ms) → uint32_t`** *(line 62)*
Polls button (active-low, INPUT_PULLUP) every 10 ms:
1. Waits for button to go LOW (press detected)
2. Waits for button to go HIGH (release detected)
3. Returns press duration in ms, or 0 if timeout

#### `setup()` *(line 78)*

Complete fob lifecycle in a single function:

1. `Serial.begin(115200)`, 50 ms settle
2. `led::init()` — configure RGB pins
3. `pinMode(UGUISU_PIN_BUTTON, INPUT_PULLUP)`
4. `g_store.begin()` — fatal `led::error_loop()` on failure
5. `load_provisioning()` — load key from flash
6. If VBUS present: spawn `prov_led_task` (blue LED, FreeRTOS, 128-word stack, priority 1)
7. `ensure_provisioned()` — listen for USB provisioning
8. Clean up: delete FreeRTOS LED task, turn LEDs off
9. Print `BOOTED:Uguisu`
10. `Bluefruit.begin()`, set name "Uguisu", TX power 0 dBm
11. `wait_for_button_press_release(10 s)` — if 0 (timeout), `system_off()`
12. Determine command: press >= 1000 ms → Lock, else Unlock
13. Select LED pin: Lock → RED, Unlock → GREEN
14. `readVbat_mv()` — check if below 3400 mV threshold
15. `g_store.loadLast()` — get last counter, increment by 1
16. Build nonce and message, compute MIC via `ccm_mic_8()` — if fails, `system_off()`
17. Assemble 13-byte payload: msg(5) + mic(8)
18. `g_store.update(counter)` — persist counter
19. `start_advertising_once()` — begin BLE broadcast
20. LED feedback: `flash_low_battery()` (rapid pulses) or `flash_once()` (single pulse)
21. Wait for remaining `UGUISU_ADVERTISE_MS` (600 ms total)
22. `system_off()` — deep sleep

#### `loop()` *(line 148)*

Empty — Uguisu completes its entire lifecycle in `setup()` and enters system-off before reaching `loop()`.

---

## 5. BLE Protocol

### Advertisement Payload Structure

| Offset | Size | Field | Encoding |
|--------|------|-------|----------|
| 0 | 2 | Company ID | Little-endian (0xFFFF) |
| 2 | 4 | Counter | Little-endian uint32 |
| 6 | 1 | Command | 0x01=Unlock, 0x02=Lock |
| 7 | 8 | MIC | AES-128-CCM tag |

**Total MSD:** 15 bytes (2 + 13)

### Cryptographic Parameters

| Parameter | Value |
|-----------|-------|
| Cipher | AES-128 (nRF52840 hardware ECB) |
| Mode | CCM (Counter with CBC-MAC) |
| Key size | 128 bits (16 bytes) |
| Nonce | 13 bytes: LE32(counter) + 9 zero bytes |
| Plaintext | 5 bytes: LE32(counter) + command |
| Tag length | 8 bytes (M=8) |
| L field | 2 (length-field size in CCM) |

### Anti-Replay Mechanism

- Both Uguisu and Guillemot maintain independent counter logs
- Uguisu increments counter on each button press: `new = lastCounter + 1`
- Guillemot rejects any payload where `counter <= lastCounter`
- Counters persist across power cycles via LittleFS log files
- Provisioning seeds both devices to the same starting counter

---

## 6. Hardware Design — Guillemot PCB

**KiCad version:** 9.0 (file format 20250114)

The Guillemot receiver is a purpose-built 4-layer PCB that handles high-voltage scooter battery power (up to ~42V) and low-power BLE scanning. It integrates power switching, SR latch state retention, buck regulation, buzzer feedback, and the XIAO nRF52840 module.

### Guillemot KiCad Project

**Files:**
- `Guillemot/hardware/Guillemot.kicad_sch` — Schematic (single root sheet)
- `Guillemot/hardware/Guillemot.kicad_pcb` — PCB layout
- `Guillemot/hardware/Guillemot.kicad_pro` — Project configuration + DRC rules
- `Guillemot/hardware/Guillemot.kicad_prl` — Editor layout state

**Project Settings (`.kicad_pro`):**

| Parameter | Value |
|-----------|-------|
| Default net class clearance | 0.2 mm |
| Default track width | 0.2 mm |
| Default via diameter / drill | 0.6 mm / 0.3 mm |
| Min via annular width | 0.1 mm |
| Min via diameter | 0.5 mm |
| Min hole clearance | 0.25 mm |
| Min hole-to-hole | 0.25 mm |
| Min copper-to-edge | 0.5 mm |
| Solder mask to copper clearance | 0.005 mm |
| Min text height | 0.8 mm |
| Track width presets | 0.2 mm (default), 0.5 mm, 1.0 mm |
| Teardrop vias | Enabled (on PTH pads, SMD pads, and vias) |

**Symbol Libraries Used:**
- `Connector:TestPoint`
- `Device:C`, `Device:D_TVS`, `Device:D_Zener`, `Device:R`
- `Diode:1N4148`
- `Seeed_Studio_XIAO_Series:XIAO-nRF52840-SMD`
- `Transistor_BJT:MMBT3904`
- `Transistor_FET:2N7002`, `Q_NMOS_GSD`, `Q_PMOS_GSD`
- `easyeda2kicad:FST-1230` (buzzer)
- `easyeda2kicad:IPB042N10N3G` (main power MOSFET)
- `easyeda2kicad:SN74LVC2G02DCTR` (dual NOR gate SR latch)
- `easyeda2kicad:TPSM365R6V3RDNR` (buck converter module)
- `power:+BATT`, `power:GND`

---

### Guillemot Schematic Analysis

**33 named nets** organized into functional subcircuits:

#### Power Path

| Net | Function |
|-----|----------|
| `+BATT` | Scooter battery positive (up to 42V) |
| `GND` | System ground |
| `10V_RAW` | Buck converter input after TVS protection |
| `10V_RAIL` | Regulated 10V rail (buck output stage) |
| `3.3V` | 3.3V rail for MCU and logic (buck output) |

#### Power Switching

| Net | Function |
|-----|----------|
| `ISO_GATE` | Main MOSFET (Q1 IPB042N10N3G) gate drive — isolated from logic |
| `PFET_GATE` | P-FET (Q3 SI2309CDS) gate for bleeder circuit |
| `PFET_CTRL` | MCU-side P-FET control (via level shifter) |
| `PRECHARGE_OUT` | Output of pre-charge FET (Q2 AO3422) + 47 ohm resistor |
| `ESC-` | ESC (electronic speed controller) ground — switched by main MOSFET |

#### SR Latch Logic

| Net | Function |
|-----|----------|
| `Q_UNLOCK` | NOR latch Q output — HIGH when unlocked |
| `QBAR_LOCK` | NOR latch Q-bar output — HIGH when locked |
| `MCU_D0` | XIAO D0 → latch SET input (unlock trigger) |
| `MCU_D1` | XIAO D1 → latch RESET input (lock trigger) |

#### Buzzer

| Net | Function |
|-----|----------|
| `NPN_BASE` | Base drive for push-pull buzzer driver (Q5, Q6 MMBT3904) |
| `BUZZER_SINK` | Buzzer low-side current sink |
| `MCU_D3` | XIAO D3 → buzzer PWM source |

#### Circuit Topology (Signal Flow)

```
+BATT ──→ D5 (TVS, SMBJ45A) ──→ 10V_RAW
  │
  ├── PS1 (TPSM365R6V3RDNR) ──→ 3.3V (to U1 XIAO, U2 NOR latch)
  │
  ├── R8 (47Ω pre-charge) + Q2 (AO3422) ──→ PRECHARGE_OUT
  │       └── Controlled by Q_UNLOCK via R/C delay
  │
  └── Q1 (IPB042N10N3G, 100V/137A)
        Gate ← ISO_GATE ← D3/D4 (1N4148 isolation) ← Q_UNLOCK
        Drain ← +BATT
        Source → ESC- (scooter load)

U2 (SN74LVC2G02DCTR) — Dual NOR SR Latch:
  SET  ← MCU_D0 (via level shift) → Q_UNLOCK
  RESET ← MCU_D1 (via level shift) → QBAR_LOCK
  VCC = 3.3V

Q3 (SI2309CDS, P-FET bleeder):
  Gate ← PFET_GATE ← R4 (1M pull-up) + Q4 (2N7002)
  Source ← +BATT
  Drain → R5 (10k) → GND
  Purpose: 10kΩ bleeder dissipates charge when locked

Buzzer Driver (push-pull):
  MCU_D3 → NPN_BASE → Q5/Q6 (MMBT3904 pair)
  BZ1 (FUET-1230) between 10V_RAIL and BUZZER_SINK
  R6 (4.7k) discharge resistor across buzzer
```

#### Key Design Points

- **D5 (SMBJ45A):** 45V TVS on battery input absorbs transients from regenerative braking
- **D1 (BZT52C12-7-F):** 12V Zener clamps buck input to safe range
- **D2 (BZT52C10-7-F):** 10V Zener on the regulated rail
- **D3, D4 (1N4148W):** Gate isolation diodes prevent backfeed between latch and MOSFET gate
- **C1 (10nF) + R3 (100k):** POR differentiator (RC tau = 1 ms) — generates a reset pulse on power-up to initialize latch in locked state
- **C2 (100uF):** Bulk decoupling on buck output
- **C3, C4 (1uF):** RC delay bypass and gate isolation filtering
- **R8 (47 ohm, 2W):** Pre-charge current limiter — caps inrush to ~0.8A (42V/47 ohm)

---

### Guillemot PCB Layout

**Board Dimensions:** ~55.5 x 42 mm (with rounded corners, r ~ 2 mm)

#### Layer Stackup (4-layer)

| Layer | Type | Thickness | Purpose |
|-------|------|-----------|---------|
| F.Cu | Signal | 0.035 mm (1 oz) | Top signal routing |
| Prepreg 1 | FR4 dielectric | 0.1 mm | er=4.5, tan_d=0.02 |
| In1.Cu | Power (GND_PLANE) | 0.035 mm | Solid ground plane |
| Core | FR4 dielectric | 1.24 mm | er=4.5, tan_d=0.02 |
| In2.Cu | Power (PWR_PLANE) | 0.035 mm | Power distribution plane |
| Prepreg 3 | FR4 dielectric | 0.1 mm | er=4.5, tan_d=0.02 |
| B.Cu | Signal | 0.035 mm (1 oz) | Bottom signal routing |
| F.Mask / B.Mask | Solder mask | 0.01 mm each | |

**Total thickness:** ~1.6 mm

#### Component Footprints

| Ref | Footprint | Package |
|-----|-----------|---------|
| U1 | Seeed_Studio_XIAO_Series:XIAO-nRF52840-SMD | Castellated module |
| U2 | MSOP-8 (2.9x2.8 mm, 0.65 mm pitch) | Logic IC |
| PS1 | QFN-11 (4.5x3.5 mm, 0.5 mm pitch) | Buck converter module |
| Q1 | TO-263-2 (D2PAK, 10x9.1 mm) | Main power MOSFET |
| Q2-Q4 | SOT-23 | Small-signal FETs |
| Q5, Q6 | SOT-23 | NPN BJTs |
| BZ1 | Custom SMD 12x12 mm | Piezo buzzer |
| D1-D4 | SOD-123 | Signal/Zener diodes |
| D5 | SMB (DO-214AA) | TVS diode |
| R1-R7 | 0603 (1608 Metric) | Resistors |
| R8 | 2512 (6432 Metric) | 2W power resistor |
| C1, C3, C4 | 0603 (1608 Metric) | Capacitors |
| C2 | 1210 (3225 Metric) | 100uF bulk cap |
| TP1 | 6x6 mm pad | BAT- (XT60 wire) |
| TP2-TP4 | 6x6 mm pad | ESC-, ESC+ (XT60 wires) |
| TP5-TP7 | 4x4 mm pad | WIRE, 3.3V, GND test points |

#### Mounting Holes

Two mounting circles visible in Edge.Cuts layer:
- Center (139, 74.5), radius 1.7 mm
- Center (92.25, 74.5), radius 1.7 mm

#### Design Rules Applied

- Teardrops on PTH pads, SMD pads, and vias (height ratio 1.0, length ratio 0.5)
- Solder mask tenting on both front and back
- No solder mask bridges in footprints

---

### Guillemot Bill of Materials

**28 unique component placements, ~$20 total BOM cost.**

| Ref | Value | Part Number | Package | LCSC | Qty | Role |
|-----|-------|-------------|---------|------|-----|------|
| U1 | XIAO-nRF52840-SMD | — | Castellated | — | 1 | BLE MCU module |
| U2 | SN74LVC2G02DCTR | SN74LVC2G02DCTR | MSOP-8 | C94600 | 1 | Dual NOR SR latch |
| PS1 | TPSM365R6V3RDNR | TPSM365R6V3RDNR | QFN-11 | C18208843 | 1 | 3-65V to 3.3V buck, 600mA, 4uA Iq |
| Q1 | IPB042N10N3G | IPB042N10N3G | TO-263-2 | C69300 | 1 | 100V/137A N-FET, 4.2 mohm Rdson |
| Q2 | AO3422 | AO3422 | SOT-23 | C37130 | 1 | Pre-charge N-FET, 55V, 160 mohm |
| Q3 | SI2309CDS | SI2309CDS | SOT-23 | C10493 | 1 | Bleeder P-FET, 60V, 450 mohm |
| Q4 | 2N7002 | 2N7002 | SOT-23 | C8545 | 1 | Gate driver N-FET, 60V |
| Q5, Q6 | MMBT3904 | MMBT3904 | SOT-23 | C20526 | 2 | NPN buzzer push-pull driver |
| BZ1 | FUET-1230 | FUET-1230 | 12x12 mm SMD | C391037 | 1 | Piezo buzzer, 4 kHz, 75 dB |
| D1 | BZT52C12-7-F | BZT52C12-7-F | SOD-123 | C124196 | 1 | 12V Zener, 370 mW |
| D2 | BZT52C10-7-F | BZT52C10-7-F | SOD-123 | C155227 | 1 | 10V Zener, 370 mW |
| D3, D4 | 1N4148W | 1N4148W | SOD-123 | C81598 | 2 | Fast switching diode, 100V |
| D5 | SMBJ45A | SMBJ45A | SMB | C114005 | 1 | 45V TVS, 600W peak |
| C1 | 10nF | — | 0603 | — | 1 | POR differentiator |
| C2 | 100uF | EMK325ABJ107MM-P | 1210 | C90143 | 1 | Bulk decoupling, 16V X5R |
| C3, C4 | 1uF | — | 0603 | — | 2 | RC bypass / gate filter |
| R1-R3, R7 | 100k | — | 0603 | — | 4 | Gate drive, pull-ups, RC timing |
| R4 | 1M | — | 0603 | — | 1 | P-FET gate pull-up |
| R5 | 10k | — | 0603 | — | 1 | Bleeder load resistor |
| R6 | 4.7k | — | 0603 | — | 1 | Buzzer discharge |
| R8 | 47 ohm 2W | — | 2512 | C136992 | 1 | Pre-charge current limit |
| TP1-TP4 | — | — | 6x6 mm pad | — | 4 | BAT-, ESC-, ESC+, WIRE |
| TP5-TP7 | — | — | 4x4 mm pad | — | 3 | 3.3V, GND test points |

---

### Guillemot Production Files

**Path:** `Guillemot/hardware/production/`

| File | Size | Content |
|------|------|---------|
| `Guillemot.zip` | 86 KB | Gerber files (all layers) for PCB fabrication |
| `Guillemot_bom.csv` | 990 B | BOM with designators, footprints, values, LCSC part numbers |
| `Guillemot_positions.csv` | 814 B | Pick-and-place: X/Y coordinates, rotation, layer for all 28 placements |
| `Guillemot_designators.csv` | 176 B | Reference designator index (33 entries) |
| `netlist.ipc` | 18 KB | IPC-2581 netlist for CAM/DFM verification |
| `backups/` | — | Timestamped project snapshots |

**Assembly notes:** All SMT components on top layer. XIAO castellated edges, XT60 pigtails, and test point wires are hand-soldered. JLCPCB P&P compatible for all LCSC-sourced parts.

---

## 7. Hardware Design — Uguisu PCB

**KiCad version:** 9.0 (file format 20250114)

The Uguisu fob is a minimal 2-layer PCB carrying only the XIAO nRF52840 module, one tactile switch, one RGB LED, and three current-limiting resistors. Power comes from a 3.7V LiPo charged via the XIAO's onboard USB-C charger.

### Uguisu KiCad Project

**Files:**
- `Uguisu/hardware/Uguisu.kicad_sch` — Schematic (single root sheet)
- `Uguisu/hardware/Uguisu.kicad_pcb` — PCB layout
- `Uguisu/hardware/Uguisu.kicad_pro` — Project configuration + DRC rules

**Project Settings (`.kicad_pro`):**

| Parameter | Value |
|-----------|-------|
| Default net class clearance | 0.2 mm |
| Default track width | 0.2 mm |
| Default via diameter / drill | 0.6 mm / 0.3 mm |
| Default pad size | 2.54 x 1.27 mm (PTH, 0.8 mm drill) |
| Track width presets | 0.2 mm (default), 0.5 mm |
| Teardrop vias | Enabled (on PTH pads, SMD pads, and vias) |

**Symbol Libraries Used:**
- `Connector:TestPoint`
- `Device:R`
- `Seeed_Studio_XIAO_Series:XIAO-nRF52840-SMD`
- `easyeda2kicad:SKQGABE010` (tactile switch)
- `easyeda2kicad:XL-5050RGBC` (RGB LED, common anode, 5050 package)
- `power:+BATT`, `power:GND`

---

### Uguisu Schematic Analysis

**18 named nets** in a simple topology:

#### Net List

| Net | Function |
|-----|----------|
| `+BATT` | 3.7V LiPo positive (to XIAO BAT+ pin) |
| `GND` | System ground |
| `3.3V` | XIAO regulated 3.3V output |
| `MCU_D0` | Button input (active-low, internal pull-up) |
| `MCU_D1` | Blue LED sink (via R1 120 ohm) |
| `MCU_D2` | Red LED sink (via R2 330 ohm) |
| `MCU_D3` | Green LED sink (via R3 120 ohm) |

#### Circuit Topology

```
+BATT (3.7V LiPo) ──→ XIAO BAT+ (onboard charger + LDO → 3.3V)

KEY1 (SKQGABE010):
  Pin 1 → MCU_D0 (XIAO D0 / P0.02)
  Pin 2 → GND
  Internal pull-up enabled in firmware

LED1 (XL-5050RGBC, common anode):
  Anode → 3.3V
  Blue cathode  → R1 (120Ω) → MCU_D1 (XIAO D1)
  Red cathode   → R2 (330Ω) → MCU_D2 (XIAO D2)
  Green cathode → R3 (120Ω) → MCU_D3 (XIAO D3)

Test Points:
  TP1 → GND
  TP2 → 3.3V
```

#### LED Current Limiting

| Color | Vf (typ) | Resistor | I_led |
|-------|----------|----------|-------|
| Blue | ~3.0V | 120 ohm | (3.3 - 3.0) / 120 = 2.5 mA |
| Red | ~2.0V | 330 ohm | (3.3 - 2.0) / 330 = 3.9 mA |
| Green | ~3.0V | 120 ohm | (3.3 - 3.0) / 120 = 2.5 mA |

All LED currents kept under 5 mA for battery longevity. Active-low drive: GPIO LOW = LED ON (current sinks through MCU pin).

---

### Uguisu PCB Layout

**Board Dimensions:** ~35 x 46 mm (with rounded corners, r ~ 2 mm)

#### Layer Stackup (2-layer)

| Layer | Type | Purpose |
|-------|------|---------|
| F.Cu | Signal | Top signal routing + component pads |
| B.Cu | Signal | Bottom routing |
| F.Mask / B.Mask | Solder mask | |
| F.SilkS / B.SilkS | Silkscreen | Reference designators |

**Note:** No internal power planes — the minimal component count and low current requirements don't warrant a 4-layer stackup.

#### Component Footprints

| Ref | Footprint | Package |
|-----|-----------|---------|
| U1 | Seeed_Studio_XIAO_Series:XIAO-nRF52840-SMD | Castellated module |
| KEY1 | SKQGABE010 | SMD-4P 5.2x5.2 mm tactile switch |
| LED1 | XL-5050RGBC | SMD5050-6P RGB LED |
| R1, R3 | 0603 (1608 Metric) | 120 ohm current-limiting |
| R2 | 0603 (1608 Metric) | 330 ohm current-limiting |
| TP1, TP2 | 3.0x3.0 mm pad | GND, 3.3V test points |

#### Mounting

Two mounting holes visible in Edge.Cuts (center ~105.3, 107.0, radius 1.6 mm).

---

### Uguisu Bill of Materials

**8 unique component placements, ~$10 total BOM cost (including LiPo).**

| Ref | Value | Part Number | Package | Qty | Role |
|-----|-------|-------------|---------|-----|------|
| U1 | XIAO-nRF52840-SMD | — | Castellated | 1 | BLE MCU (same as Guillemot) |
| KEY1 | SKQGABE010 | SKQGABE010 | SMD 5.2x5.2 | 1 | Tactile button, active-low |
| LED1 | XL-5050RGBC | XL-5050RGBC | SMD 5050-6P | 1 | RGB LED, common anode |
| R1, R3 | 120 ohm | — | 0603 | 2 | Blue/Green LED current limit |
| R2 | 330 ohm | — | 0603 | 1 | Red LED current limit |
| TP1, TP2 | — | — | 3x3 mm pad | 2 | GND, 3.3V test points |
| (BAT) | TW-502535 | — | Wire solder | 1 | 3.7V 400mAh LiPo (off-board) |

**Fabrication config** (`fabrication-toolkit-options.json`): Configured for JLCPCB Fabrication Toolkit export with auto-translate, auto-fill, and browser preview enabled.

---

## 8. Build System

Both firmware projects use identical PlatformIO configurations:

| Setting | Value |
|---------|-------|
| Platform | `https://github.com/maxgerhardt/platform-nordicnrf52` |
| Board | `xiaoble_adafruit` (Seeed XIAO nRF52840) |
| Framework | Arduino |
| Monitor speed | 115200 |
| LDF mode | `deep+` (deep dependency resolution) |

**Dependencies:**
- `afantor/Bluefruit52_Arduino` — nRF52 BLE stack
- `adafruit/Adafruit TinyUSB Library` — USB CDC serial
- `SPI` — required by Bluefruit52
- `ImmoCommon=symlink://../../lib` — shared library

**Build flags:**
- `-D CFG_TUSB_CONFIG_FILE=\"immo_tusb_config.h\"` — custom TinyUSB config
- `-I include` — project-specific headers
- `-I ../../lib` — shared library headers
- `-I .pio/libdeps/.../Adafruit TinyUSB Library/src` — TinyUSB include path

---

## 9. CI/CD Pipeline

**File:** `.github/workflows/release.yml`

**Trigger:** Push of a `v*` tag (e.g., `v1.0.0`)

**Steps:**
1. Checkout repository
2. Set up Python 3.11
3. Install PlatformIO
4. Build Guillemot firmware (`pio run` in `Guillemot/firmware/`)
5. Build Uguisu firmware (`pio run` in `Uguisu/firmware/`)
6. Rename artifacts: `guillemot-{version}.hex`, `guillemot-{version}.zip`, `uguisu-{version}.hex`, `uguisu-{version}.zip`
7. Create GitHub release with all four artifacts via `softprops/action-gh-release@v2`

The `.hex` files are for J-Link/SWD flashing; `.zip` files are for DFU (OTA) updates.

---

## 10. Tools

### gen_mic.py

**Path:** `tools/test_vectors/gen_mic.py` (73 lines)

Python test vector generator for validating firmware crypto implementation.

**Usage:**
```
python3 gen_mic.py --counter 1 --command 0x01 --key 00112233...ccddeeff
```

**Arguments:**
| Flag | Type | Required | Description |
|------|------|----------|-------------|
| `--counter` | int | Yes | Counter value (hex/decimal) |
| `--command` | int | Yes | Command byte (0x01 or 0x02) |
| `--company-id` | int | No | MSD company ID (default 0xFFFF) |
| `--key` | hex string | Yes | 32 hex chars (16-byte key) |

**Output:** Prints nonce, msg, mic, payload_13B, and msd_company_plus_payload in hex.

**Implementation:** Uses Python `cryptography` library's `AESCCM` with `tag_length=8`. Helper functions `le16()` and `le32()` for little-endian encoding.

### HTML Visual Tools

| Tool | Purpose |
|------|---------|
| `led_visualizer.html` | Interactive preview of LED fade-in/hold/fade-out timing curves |
| `buzzer_tuner.html` | Frequency and duration tuner for buzzer tones |
| `ble_timing_simulator.html` | Visualizes BLE scan window vs advertisement interval overlap |

---

## 11. Module Dependency Graph

```
┌─────────────────────────────────────────────────────┐
│                  Guillemot (Receiver)                │
│  main.cpp                                           │
│  ├── guillemot_config.h → guillemot_nrf52840.h      │
│  │   └── latch_set_pulse(), latch_reset_pulse()     │
│  └── ImmoCommon.h ──┐                               │
│      ├── immo_crypto │ verify MIC                   │
│      ├── immo_provisioning │ load/provision key      │
│      ├── immo_storage │ anti-replay counter log      │
│      └── immo_util │ is_key_blank, led_error_loop    │
│  External: Bluefruit52 (BLE scanner)                │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│                   Uguisu (Key Fob)                   │
│  main.cpp                                           │
│  ├── uguisu_config.h → uguisu_nrf52840.h            │
│  │   └── readVbat_mv(), system_off()                │
│  ├── led_effects.h                                  │
│  │   └── LED animation (flash, pulse, error_loop)   │
│  └── ImmoCommon.h ──┐                               │
│      ├── immo_crypto │ generate MIC                  │
│      ├── immo_provisioning │ load/provision key      │
│      ├── immo_storage │ counter log                  │
│      └── immo_util │ is_key_blank                    │
│  External: Bluefruit52 (BLE advertiser), FreeRTOS   │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│                lib/ (ImmoCommon)                     │
│  immo_crypto ──→ nrf_soc.h (sd_ecb_block_encrypt)  │
│  immo_provisioning ──→ immo_crypto, immo_storage    │
│                     ──→ Adafruit_LittleFS           │
│  immo_storage ──→ Adafruit_LittleFS                 │
│  immo_util ──→ Arduino.h                            │
│  immo_tusb_config ──→ tusb_config_nrf.h             │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│              tools/ (Development Aids)               │
│  gen_mic.py ──→ cryptography (Python AESCCM)        │
│  *.html ──→ standalone browser tools                │
└─────────────────────────────────────────────────────┘
```
