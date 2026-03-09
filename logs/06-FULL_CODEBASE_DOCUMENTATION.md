# BLE Immobilizer System - Complete Codebase Documentation

## System Overview

The BLE immobilizer system for the Ninebot G30 scooter consists of four interconnected projects:

| Project | Role | Platform | Language |
|---------|------|----------|----------|
| **Guillemot** | Receiver (on scooter) | Seeed XIAO nRF52840 | C++ (Arduino/PlatformIO) |
| **Uguisu** | Fob (key fob) | Seeed XIAO nRF52840 | C++ (Arduino/PlatformIO) |
| **ImmoCommon** | Shared library | nRF52840 | C++ |
| **Whimbrel** | Web provisioning app | Browser | JavaScript (vanilla) |

**Security Model:** AES-128-CCM authenticated BLE advertisements with monotonic counter-based replay prevention.

**Data Flow:**
```
User presses fob button
  -> Uguisu signs command with AES-128-CCM
  -> BLE advertisement broadcast (15-byte MSD)
  -> Guillemot scans, verifies MIC, checks counter
  -> Latch relay triggered (unlock/lock)
```

**Provisioning Flow:**
```
Whimbrel (browser) generates 128-bit key via Web Crypto API
  -> USB Serial to Uguisu: PROV:<key>:<counter>:<crc16>
  -> USB Serial to Guillemot: PROV:<key>:<counter>:<crc16>
  -> Both devices share same PSK for AES-CCM
```

---

# 1. ImmoCommon - Shared Library

**Location:** `ImmoCommon/src/`

ImmoCommon is the shared C++ library used by both Guillemot and Uguisu. It provides cryptography, provisioning, and counter storage. All code lives in the `immo` namespace.

---

## 1.1 ImmoCommon.h - Aggregator Header

**File:** `ImmoCommon/src/ImmoCommon.h`

Single include file that re-exports all submodules:

```cpp
#include "immo_crypto.h"
#include "immo_provisioning.h"
#include "immo_storage.h"
```

---

## 1.2 Cryptography Module

### 1.2.1 immo_crypto.h

**File:** `ImmoCommon/src/immo_crypto.h`

**Constants:**

| Name | Value | Description |
|------|-------|-------------|
| `MIC_LEN` | 8 | AES-CCM authentication tag length (bytes) |
| `MSG_LEN` | 5 | Message: 4-byte counter + 1-byte command |
| `PAYLOAD_LEN` | 13 | MSG_LEN + MIC_LEN |
| `NONCE_LEN` | 13 | 4-byte counter LE + 9 zero bytes |

**Enum `Command : uint8_t`:**

| Value | Name | Description |
|-------|------|-------------|
| `0x01` | `Unlock` | Unlock the scooter latch |
| `0x02` | `Lock` | Lock the scooter latch |

**Struct `Payload`:**

| Field | Type | Description |
|-------|------|-------------|
| `counter` | `uint32_t` | Rolling anti-replay counter |
| `command` | `Command` | Unlock or Lock |
| `mic` | `uint8_t[8]` | AES-128-CCM authentication tag |

**Functions:**

- **`build_nonce(uint32_t counter, uint8_t nonce[13])`** - Constructs CCM nonce: counter as LE32 at bytes 0-3, zeros at bytes 4-12.

- **`build_msg(uint32_t counter, Command command, uint8_t msg[5])`** - Constructs authenticated message: counter as LE32 at bytes 0-3, command byte at byte 4.

- **`ccm_mic_8(const uint8_t key[16], const uint8_t nonce[13], const uint8_t* msg, size_t msg_len, uint8_t out_mic[8]) -> bool`** - Computes AES-128-CCM 8-byte authentication tag. Uses L=2, M=8 parameters. Returns false on AES encryption failure.

- **`constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n) -> bool`** - Timing-attack resistant byte comparison. XORs all bytes into an accumulator, returns true only if result is zero.

### 1.2.2 immo_crypto.cpp

**File:** `ImmoCommon/src/immo_crypto.cpp`

**Imports:** `immo_crypto.h`, `<nrf_soc.h>`, `<string.h>`

**Internal helpers (anonymous namespace):**

- **`le16_write(uint8_t out[2], uint16_t x)`** - Writes 16-bit value as little-endian.

- **`le32_write(uint8_t out[4], uint32_t x)`** - Writes 32-bit value as little-endian.

- **`aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) -> bool`** - Hardware-accelerated AES-128 ECB via `sd_ecb_block_encrypt()` (Nordic SoftDevice).

- **`xor_block(uint8_t dst[16], const uint8_t a[16], const uint8_t b[16])`** - XORs two 16-byte blocks.

**`ccm_mic_8` implementation detail:**
1. Validates `msg_len <= 0xFFFF`
2. Builds B0 block: flags byte (`0x3A`), 13-byte nonce, 2-byte message length (big-endian)
3. CBC-MAC phase: iterates over B0 and message blocks, XOR + AES-ECB encrypt
4. Counter block A0: flags byte (`0x01`), 13-byte nonce, 2-byte zero counter
5. Encrypts A0 to get S0, XORs first 8 bytes of CBC-MAC with S0 for final MIC

---

## 1.3 Provisioning Module

### 1.3.1 immo_provisioning.h

**File:** `ImmoCommon/src/immo_provisioning.h`

**Functions:**

- **`prov_is_vbus_present() -> bool`** - Detects USB VBUS power on nRF52840/nRF52833 via `NRF_POWER->USBREGSTATUS`. Returns false on other platforms.

- **`prov_run_serial_loop(uint32_t timeout_ms, bool (*on_success)(const uint8_t key[16], uint32_t counter)) -> bool`** - Waits for provisioning command on Serial within timeout. Parses format `PROV:<key_32_hex>:<counter_8_hex>:<checksum_4_hex>`. Validates CRC-16-CCITT of key bytes against checksum. Calls `on_success` callback. Returns true on success. Sends `ACK:PROV_SUCCESS` or `ERR:*` responses.

- **`ensure_provisioned(uint32_t timeout_ms, bool (*on_success)(...), void (*load_provisioning)(), bool (*is_provisioned)())`** - Orchestrates full provisioning flow. If VBUS present, runs serial loop once with timeout. Then loops indefinitely if still not provisioned and VBUS present. Returns when provisioned or VBUS disconnected.

### 1.3.2 immo_provisioning.cpp

**File:** `ImmoCommon/src/immo_provisioning.cpp`

**Imports:** `immo_provisioning.h`, `<Arduino.h>`

**Internal helpers (anonymous namespace):**

- **`hex_byte(const char* hex, uint8_t* out) -> bool`** - Parses two hex ASCII characters to byte. Supports 0-9, a-f, A-F.

- **`parse_counter_hex(const char* hex, uint32_t* out) -> bool`** - Parses 8 hex characters into 32-bit counter value.

- **`crc16_ccitt(const uint8_t* data, size_t len) -> uint16_t`** - CRC-16-CCITT (polynomial 0x1021, init 0xFFFF). Used to validate provisioning payload integrity.

**`prov_run_serial_loop` implementation:**
1. Reads characters into 128-byte buffer until newline or timeout
2. Validates `PROV:` prefix and two colon delimiters
3. Checks field lengths: 32 (key), 8 (counter), 4 (checksum)
4. Parses key bytes, counter value, and checksum
5. Computes CRC-16-CCITT of key, compares to received checksum
6. On match: calls `on_success(key_buf, counter_val)`
7. Serial responses: `ACK:PROV_SUCCESS`, `ERR:MALFORMED`, `ERR:CHECKSUM`, `ERR:STORAGE`

---

## 1.4 Storage Module

### 1.4.1 immo_storage.h

**File:** `ImmoCommon/src/immo_storage.h`

**Struct `CounterRecord`:**

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `counter` | `uint32_t` | 4 bytes | Counter value |
| `crc32` | `uint32_t` | 4 bytes | IEEE CRC-32 of counter (integrity check) |

**Class `CounterStore`:**

Manages persistent anti-replay counter storage with log rotation on LittleFS.

**Constructor:** `CounterStore(const char* log_path, const char* old_log_path, size_t max_bytes)`

**Private members:**

| Member | Type | Description |
|--------|------|-------------|
| `log_path_` | `const char*` | Primary log file path |
| `old_log_path_` | `const char*` | Rotated (backup) log path |
| `max_bytes_` | `size_t` | Log size threshold for rotation |
| `last_counter_` | `uint32_t` | Cached last valid counter |

**Public methods:**

- **`begin() -> bool`** - Initializes InternalFS (LittleFS). Returns success/failure.

- **`load()`** - Scans log file sequentially, validates CRC-32 of each 8-byte record, updates `last_counter_` to highest valid value. Skips corrupted records (power-loss resilient).

- **`lastCounter() const -> uint32_t`** - Returns cached last counter (0 if none loaded).

- **`loadLast() -> uint32_t`** - Convenience: calls `load()` then returns `lastCounter()`.

- **`update(uint32_t counter)`** - Rotates log if needed, creates `CounterRecord` with CRC-32, appends to log file, flushes to flash, updates cached value.

- **`seed(uint32_t counter)`** - Removes both log files, writes single new record. Used during provisioning to reset counter state.

**Private methods:**

- **`rotateIfNeeded_()`** - If log file size >= `max_bytes_`, removes old log and renames current to old. Creates space for fresh log.

### 1.4.2 immo_storage.cpp

**File:** `ImmoCommon/src/immo_storage.cpp`

**Imports:** `immo_storage.h`, `<Adafruit_LittleFS.h>`, `<InternalFileSystem.h>`

**Internal helpers (anonymous namespace):**

- **`crc32_ieee(const uint8_t* data, size_t len) -> uint32_t`** - IEEE CRC-32 (polynomial 0xEDB88320, init 0xFFFFFFFF, final invert).

- **`record_crc(uint32_t counter) -> uint32_t`** - Computes CRC-32 over 4 counter bytes.

---

## 1.5 TinyUSB Configuration

**File:** `ImmoCommon/src/immo_tusb_config.h`

Disables TinyUSB Mass Storage Class (device and host) to prevent `SdFat::File` / `Adafruit_LittleFS::File` class collision:

```cpp
#undef CFG_TUD_MSC
#define CFG_TUD_MSC 0
#undef CFG_TUH_MSC
#define CFG_TUH_MSC 0
```

---

## 1.6 Test Vector Generator

**File:** `ImmoCommon/tools/test_vectors/gen_mic.py`

Python utility for generating reference AES-128-CCM test vectors.

**Imports:** `argparse`, `binascii`, `cryptography.hazmat.primitives.ciphers.aead.AESCCM`

**Functions:**

- **`le16(x) -> bytes`** - 16-bit to 2-byte little-endian.
- **`le32(x) -> bytes`** - 32-bit to 4-byte little-endian.
- **`parse_int(s) -> int`** - Parses decimal or hex (0x prefix) string.
- **`parse_key(hex_str) -> bytes`** - Validates and parses 32-hex-char key.

**CLI arguments:** `--counter`, `--command`, `--company-id` (default 0xFFFF), `--key`

**Output:** Prints hex values for nonce, msg, mic, payload (13B), and full MSD (15B).

---

# 2. Guillemot - BLE Receiver

**Location:** `Guillemot/firmware/guillemot/`

Guillemot is the receiver firmware that runs on the scooter. It continuously scans for authenticated BLE advertisements from Uguisu and controls a bistable latch relay to lock/unlock.

---

## 2.1 Configuration

### 2.1.1 guillemot_config.h

**File:** `Guillemot/firmware/guillemot/include/guillemot_config.h`

Wrapper that includes `guillemot_config.example.h`.

### 2.1.2 guillemot_config.example.h

**File:** `Guillemot/firmware/guillemot/include/guillemot_config.example.h`

| Constant | Value | Description |
|----------|-------|-------------|
| `PIN_LATCH_SET` | `D0` | Relay control - unlock direction |
| `PIN_LATCH_RESET` | `D1` | Relay control - lock direction |
| `PIN_BUZZER` | `D3` | Buzzer/speaker output |
| `PIN_ERROR_LED` | `26` (P0.26) | Error indicator LED (-1 to disable) |
| `SCAN_INTERVAL_MS` | `2000` | BLE scan interval |
| `SCAN_WINDOW_MS` | `20` | BLE scan window |
| `MSD_COMPANY_ID` | `0xFFFF` | BLE manufacturer ID (must match fob) |
| `BUZZER_HZ` | `4000` | Buzzer tone frequency |
| `BUZZER_UNLOCK_MS` | `120` | Unlock confirmation beep duration |
| `BUZZER_LOCK_MS` | `200` | Lock confirmation beep duration |
| `LATCH_PULSE_MS` | `15` | Relay pulse width |

---

## 2.2 Main Firmware

**File:** `Guillemot/firmware/guillemot/src/main.cpp`

**Imports:** `<Arduino.h>`, `<bluefruit.h>`, `<nrf_soc.h>`, `<Adafruit_LittleFS.h>`, `<InternalFileSystem.h>`, `guillemot_config.h`, `<ImmoCommon.h>`

**Global state:**

| Variable | Type | Description |
|----------|------|-------------|
| `k_default_psk[16]` | `const uint8_t` | Default PSK (all zeros = unprovisioned) |
| `g_psk[16]` | `uint8_t` | Runtime PSK loaded from flash |
| `g_store` | `CounterStore` | Anti-replay counter store |

**File path constants:**

| Constant | Value | Description |
|----------|-------|-------------|
| `COUNTER_LOG_PATH` | `"/ctr.log"` | Primary counter log |
| `OLD_COUNTER_LOG_PATH` | `"/ctr.old"` | Rotated counter log |
| `PSK_STORAGE_PATH` | `"/psk.bin"` | PSK storage file |
| `COUNTER_LOG_MAX_BYTES` | `4096` | Log rotation threshold |
| `PROV_TIMEOUT_MS` | `30000` | Provisioning timeout (30s) |

**Functions:**

- **`on_provision_success(const uint8_t key[16], uint32_t counter) -> bool`** - Called after successful provisioning. Writes 16-byte key to `/psk.bin` on flash, verifies the write, clears counter logs, and seeds counter store with new counter. Returns true on success.

- **`key_is_all_zeros() -> bool`** - Checks if `g_psk` is all zeros (device not provisioned).

- **`load_psk_from_storage()`** - Reads 16-byte PSK from `/psk.bin`. Falls back to all-zeros default if file missing or wrong size.

- **`latch_set_pulse()`** - Triggers unlock: drives `PIN_LATCH_SET` HIGH for `LATCH_PULSE_MS` (15ms).

- **`latch_reset_pulse()`** - Triggers lock: drives `PIN_LATCH_RESET` HIGH for `LATCH_PULSE_MS` (15ms).

- **`buzzer_tone_ms(uint16_t duration_ms)`** - Plays buzzer tone at `BUZZER_HZ` for specified duration.

- **`led_error_loop() [[noreturn]]`** - Fatal error handler. Blinks error LED at 200ms intervals indefinitely.

- **`parse_payload_from_report(ble_gap_evt_adv_report_t* report, immo::Payload& out) -> bool`** - Extracts manufacturer-specific data from BLE advertisement. Validates MSD length (2 + PAYLOAD_LEN = 15 bytes) and company ID. Parses counter (LE32), command byte, and 8-byte MIC into `Payload` struct.

- **`verify_payload(const immo::Payload& pl) -> bool`** - Cryptographically verifies payload. Builds nonce and message from counter/command, computes AES-128-CCM MIC using `g_psk`, and performs constant-time comparison against received MIC.

- **`handle_valid_command(const immo::Payload& pl)`** - Processes verified commands. Rejects if counter <= last seen (replay attack). Updates counter store. Executes:
  - `Command::Unlock`: `latch_set_pulse()` + short beep
  - `Command::Lock`: long beep + `latch_reset_pulse()`

- **`scan_callback(ble_gap_evt_adv_report_t* report)`** - BLE scanner callback. Parses payload, skips if key is all-zeros, verifies MAC, and handles valid commands.

- **`setup()`** - Initialization sequence:
  1. GPIO init (latch pins, buzzer to OUTPUT, all LOW)
  2. Serial at 115200
  3. InternalFS init (fatal error loop on failure)
  4. Load PSK from storage
  5. Run provisioning if VBUS present
  6. Load counter store from log
  7. Init Bluefruit (0 peripheral, 1 central)
  8. Configure scanner (passive, MSD filter for company ID, interval/window)
  9. Start continuous BLE scan

- **`loop()`** - Calls `sd_app_evt_wait()` for low-power sleep until BLE event.

---

## 2.3 Build Configuration

**File:** `Guillemot/firmware/guillemot/platformio.ini`

| Setting | Value |
|---------|-------|
| Platform | `https://github.com/maxgerhardt/platform-nordicnrf52` |
| Board | `xiaoble_adafruit` |
| Framework | `arduino` |
| Monitor speed | 115200 |
| Dependencies | `Bluefruit52_Arduino`, `Adafruit TinyUSB Library`, `SPI` |
| Build flags | `-DGUILLEMOT_BUILD_PIO=1`, custom TinyUSB config |

---

# 3. Uguisu - BLE Fob

**Location:** `Uguisu/firmware/uguisu/`

Uguisu is the key fob firmware. On button press, it signs a command with AES-128-CCM, broadcasts it via BLE advertisement for 2 seconds, then enters deep sleep (<1 uA).

---

## 3.1 Configuration

### 3.1.1 uguisu_config.h

**File:** `Uguisu/firmware/uguisu/include/uguisu_config.h`

Wrapper that optionally includes `uguisu_config_local.h` before the example config.

### 3.1.2 uguisu_config.example.h

**File:** `Uguisu/firmware/uguisu/include/uguisu_config.example.h`

| Constant | Value | Description |
|----------|-------|-------------|
| `MSD_COMPANY_ID` | `0xFFFF` | BLE manufacturer ID |
| `UGUISU_PIN_BUTTON` | `D0` | Button input pin |
| `UGUISU_PIN_BUTTON_NRF` | `2` | NRF GPIO number for wake |
| `PIN_LED_B` | `D1` | RGB LED blue (active-low) |
| `PIN_LED_R` | `D2` | RGB LED red (active-low) |
| `PIN_LED_G` | `D3` | RGB LED green (active-low) |
| `PIN_ERROR_LED` | `26` | Error LED (P0.26) |
| `UGUISU_ADVERTISE_MS` | `2000` | Broadcast duration (2s) |
| `UGUISU_ADV_INTERVAL_MS` | `100` | Advertisement interval (100ms) |
| `UGUISU_LONG_PRESS_MS` | `1000` | Lock threshold (>=1s) |
| `UGUISU_BUTTON_TIMEOUT_MS` | `10000` | Boot wait time (10s) |

---

## 3.2 Main Firmware

**File:** `Uguisu/firmware/uguisu/src/main.cpp`

**Imports:** `<Arduino.h>`, `<bluefruit.h>`, `<nrf_gpio.h>`, `<nrf_soc.h>`, `<Adafruit_LittleFS.h>`, `<InternalFileSystem.h>`, `uguisu_config.h`, `<ImmoCommon.h>`

**Global state:**

| Variable | Type | Description |
|----------|------|-------------|
| `k_default_psk[16]` | `const uint8_t` | Default PSK (all zeros) |
| `g_psk[16]` | `uint8_t` | Runtime PSK |
| `g_store` | `CounterStore` | Counter store (`/ug_ctr.log`, `/ug_ctr.old`, 4096) |

**Additional constants:**

| Constant | Value | Description |
|----------|-------|-------------|
| `PROV_STORAGE_PATH` | `"/ug_prov.bin"` | Provisioning storage file |
| `PROV_TIMEOUT_MS` | `30000` | Provisioning timeout |
| `PROV_MAGIC` | `0x76704755` | "UGpv" magic number (LE) |

**Functions:**

- **`on_provision_success(const uint8_t key[16], uint32_t counter) -> bool`** - Writes magic number (4 bytes) + key (16 bytes) to `/ug_prov.bin`. Verifies write by re-reading and constant-time comparing. Seeds counter store. Copies key to runtime `g_psk`.

- **`key_is_all_zeros() -> bool`** - Checks if `g_psk` is all zeros (unprovisioned).

- **`load_provisioning()`** - Opens `/ug_prov.bin`, validates magic number, reads 16-byte key. Falls back to default (all-zeros) on failure.

- **`start_advertising_once(uint16_t company_id, const uint8_t payload13[13])`** - Constructs 15-byte MSD (company ID LE + 13-byte payload). Configures BLE advertisement with flags, TX power, device name, and MSD. Sets advertisement interval. Starts single-shot advertising.

- **`system_off()`** - Stops BLE advertising, configures GPIO for wake-on-button, calls `sd_power_system_off()` for deep sleep (<1 uA).

- **`wait_for_button_press_release(uint32_t timeout_ms) -> uint32_t`** - Waits for button press (active LOW) and release. Returns press duration in ms, or 0 on timeout.

- **`led_error_loop() [[noreturn]]`** - Blinks error LED indefinitely on fatal error.

- **`setup()`** - Main firmware sequence:
  1. Serial init (115200)
  2. Button pin as INPUT_PULLUP
  3. InternalFS init + counter store begin
  4. Load provisioning key
  5. Run provisioning if VBUS present
  6. Init Bluefruit, set TX power
  7. Wait for button press (up to `UGUISU_BUTTON_TIMEOUT_MS`)
  8. Determine command: short press = Unlock, long press (>=1s) = Lock
  9. If no press within timeout: `system_off()`
  10. Load last counter, increment
  11. Build nonce, message, compute AES-128-CCM MIC
  12. Construct 13-byte payload
  13. Update counter store
  14. Start BLE advertisement for 2 seconds
  15. `system_off()`

- **`loop()`** - Empty (device enters `system_off()` from setup, never reaches loop).

---

## 3.3 ImmoCommon (Local Copy)

Uguisu includes a local copy of ImmoCommon at `Uguisu/firmware/uguisu/lib/ImmoCommon/`. This is identical to the standalone ImmoCommon library (see Section 1).

---

# 4. Whimbrel - Web Provisioning App

**Location:** `Whimbrel/js/`

Whimbrel is a browser-based companion app for key provisioning and firmware flashing. It uses Web Serial API and Web Crypto API. Hosted on GitHub Pages, fully offline-capable after load.

---

## 4.1 config.js - Configuration

**File:** `Whimbrel/js/config.js`

| Constant | Value | Description |
|----------|-------|-------------|
| `GITHUB_OWNER` | `"LPFchan"` | GitHub organization |
| `BAUDRATE` | `115200` | Serial baud rate |
| `KEY_LEN_BYTES` | `16` | AES-128 key size |
| `RESET_COUNTER` | `"00000000"` | Initial counter value |
| `DEVICE_ID_FOB` | `"UGUISU_01"` | Fob device identifier |
| `DEVICE_ID_RX` | `"GUILLEMOT_01"` | Receiver device identifier |
| `BOOTED_FOB` | `"BOOTED:Uguisu"` | Fob boot confirmation message |
| `BOOTED_RX` | `"BOOTED:Guillemot"` | Receiver boot confirmation |
| `TIMEOUT_PROV_MS` | `12000` | Provisioning ACK timeout (12s) |
| `TIMEOUT_BOOT_MS` | `10000` | Boot confirmation timeout (10s) |
| `DFU_DEFAULT_MTU` | `256` | Default DFU packet size |
| `DFU_BUFFER_SIZE` | `8192` | Serial buffer size |

---

## 4.2 crypto.js - Key Generation & CRC

**File:** `Whimbrel/js/crypto.js`

**Functions:**

- **`generateKey() -> string`** - Generates 128-bit cryptographically secure random key using `crypto.getRandomValues()`. Returns 32-character lowercase hex string.

- **`crc16Key(keyHex) -> string`** - Computes CRC-16-CCITT (polynomial 0x1021, init 0xFFFF) over the 16 key bytes. Returns 4-character hex checksum.

- **`buildProvLine(keyHex) -> string`** - Builds provisioning serial command: `PROV:<keyHex>:<RESET_COUNTER>:<checksum>`. Throws if key is not exactly 32 hex characters.

---

## 4.3 serial.js - Web Serial API Wrapper

**File:** `Whimbrel/js/serial.js`

**Functions:**

- **`isSupported() -> boolean`** - Returns `"serial" in navigator`.
- **`requestPort() -> Promise<SerialPort>`** - Opens browser port selection dialog.

**Class `SerialConnection`:**

| Property | Type | Description |
|----------|------|-------------|
| `port` | `SerialPort` | Open serial port |
| `reader` | `ReadableStreamDefaultReader` | Input stream reader |
| `writer` | `WritableStreamDefaultWriter` | Output stream writer |
| `readBuffer` | `string` | Accumulated incoming text |
| `lineResolvers` | `Array` | Pending line-read promises |

**Methods:**

- **`async open(port, options?)`** - Opens port, sets up text encoder/decoder streams, starts background reader loop. Default baud: `CONFIG.BAUDRATE`.

- **`async sendLine(line)`** - Sends text with appended `\n`.

- **`async readLineWithTimeout(timeoutMs) -> string`** - Waits for one complete newline-delimited line. Throws on timeout.

- **`async close()`** - Gracefully closes port: rejects pending readers, cancels streams, releases locks, closes port.

---

## 4.4 prov.js - Provisioning Helpers

**File:** `Whimbrel/js/prov.js`

**Functions:**

- **`async waitForBooted(serialConnection, expectedBooted)`** - After provisioning ACK, waits for device to reboot and send its boot confirmation message (e.g., `BOOTED:Uguisu`). Loops reading lines until match or timeout (`CONFIG.TIMEOUT_BOOT_MS`). Throws on `ERR:` responses or timeout.

---

## 4.5 api.js - GitHub API Service

**File:** `Whimbrel/js/api.js`

**Functions:**

- **`async fetchDeviceReleases(repoName) -> Array`** - Fetches releases from `https://api.github.com/repos/LPFchan/{repoName}/releases`. Filters to only releases with `.zip` assets. Returns array of GitHub release objects.

---

## 4.6 dfu.js - Nordic Serial DFU

**File:** `Whimbrel/js/dfu.js`

**DFU Operation Codes:**

| Code | Name | Description |
|------|------|-------------|
| `0x00` | `OP_PROTOCOL_VERSION` | Get protocol version |
| `0x01` | `OP_CREATE_OBJECT` | Allocate object on device |
| `0x02` | `OP_SET_PRN` | Set packet receipt notification |
| `0x03` | `OP_CALC_CHECKSUM` | Request CRC-32 verification |
| `0x04` | `OP_EXECUTE` | Execute/install object |
| `0x06` | `OP_SELECT_OBJECT` | Select object type |
| `0x07` | `OP_MTU_GET` | Query maximum transmission unit |
| `0x08` | `OP_WRITE_OBJECT` | Write data chunk |
| `0x09` | `OP_PING` | Test connectivity |
| `0x60` | `OP_RESPONSE` | Response from bootloader |

**Object Types:** `OBJ_COMMAND = 0x01`, `OBJ_DATA = 0x02`

**Result Codes:** `RES_SUCCESS = 0x01`

**Class `SlipFramer`:**

SLIP (Serial Line IP) protocol encoder/decoder for DFU packet framing.

| Method | Description |
|--------|-------------|
| `append(chunk) -> Array<Uint8Array>` | Decodes SLIP frames from raw bytes. `0xC0` = boundary, `0xDB 0xDC` = escaped 0xC0, `0xDB 0xDD` = escaped 0xDB. |
| `static encode(packet) -> Uint8Array` | Encodes packet with SLIP framing: `[0xC0, ...data..., 0xC0]`. |

**Class `DfuFlasher`:**

**Constructor:** `new DfuFlasher(port, datBytes, binBytes)` - Takes serial port and firmware files (.dat init packet, .bin payload).

| Method | Description |
|--------|-------------|
| `async startReader()` | Starts background SLIP frame reader loop |
| `async send(data)` | SLIP-encodes and sends packet |
| `async receiveResponse(reqOpcode, timeoutMs?)` | Waits for response, validates success |
| `async sendCommandAndRead(reqOpcode, payload?)` | Send + receive convenience |
| `async ping()` | Verify bootloader connectivity |
| `async setPRN(prn)` | Set packet receipt notification (0 = disabled) |
| `async getMTU()` | Query and set MTU |
| `async selectObject(type) -> {maxSize, offset, crc}` | Prepare object write |
| `async createObject(type, size)` | Allocate object space |
| `async calcChecksum() -> {offset, crc}` | Request device CRC-32 verification |
| `async execute()` | Execute/install current object |
| `async writeObject(dataChunk)` | Write chunk split by MTU |
| `async flash(onProgress)` | **Main entry point** - Full DFU sequence |

**`flash()` sequence:**
1. Open port at 115200 with DFU_BUFFER_SIZE
2. Start SLIP reader
3. Ping bootloader
4. Disable PRN
5. Get MTU
6. **Phase 1:** Command object - select, create, write .dat, verify CRC-32, execute
7. **Phase 2:** Data object - select, get max size, loop through .bin chunks (create, write, verify, execute)
8. Cleanup (close port)
9. Progress reported via `onProgress(message, 0.0-1.0)` callback

**CRC-32 implementation:** Standard IEEE polynomial (0xEDB88320), table-based.

---

## 4.7 firmware-manager.js - Firmware ZIP Parser

**File:** `Whimbrel/js/firmware-manager.js`

**Functions:**

- **`async fetchAndParseFirmwareZip(zipUrl, arrayBufferCache?) -> {datBytes, binBytes, buffer}`** - Downloads firmware ZIP file, extracts it with JSZip, reads `manifest.json`, and returns the `.dat` (init packet) and `.bin` (firmware binary) as Uint8Arrays. Supports caching the ArrayBuffer to avoid re-download.

**Expected ZIP manifest format:**
```json
{
  "manifest": {
    "application": {
      "dat_file": "path/to/application.dat",
      "bin_file": "path/to/application.bin"
    }
  }
}
```

---

## 4.8 firmware.js - Firmware Flashing UI

**File:** `Whimbrel/js/firmware.js`

**Exported function: `initFirmwareTab(opts) -> object`**

**Parameters:** `opts` with `abortableDelay`, `animateHeightChange`, `triggerConfetti` helpers.

**Returns:**

| Method | Description |
|--------|-------------|
| `showFwStep(stepIndex, pushState)` | Navigate between firmware flow steps |
| `resetFwFlashUI()` | Reset flash UI state |
| `handleFirmwarePopState(event)` | Handle browser back button |
| `abortFwFlash()` | Abort flashing operation |
| `isFwFlashing() -> boolean` | Check if flashing in progress |
| `getFwStepIdx() -> number` | Get current step index |

**UI Steps:**

| Step | Name | Description |
|------|------|-------------|
| 0 | Device Selection | Choose Guillemot or Uguisu |
| 1 | DFU Instructions | Plug in, double-tap reset, enter bootloader |
| 2 | Flashing | Release selector, flash button, progress bar |

**Internal functions:**

- **`fetchReleases(repoName)`** - Fetches releases from GitHub, populates dropdown
- **`selectRelease(releaseData, isLatest)`** - Sets selected firmware version
- **`setFwStatus(text, progress, isError, asHtml)`** - Updates status display
- **`setFwStatusSuccessWithLink()`** - Shows success with "flash other device" link

---

## 4.9 app.js - Main Application

**File:** `Whimbrel/js/app.js`

Main UI orchestration for the Keys provisioning flow.

**State variables:**

| Variable | Type | Description |
|----------|------|-------------|
| `currentKey` | `string\|null` | Generated 32-hex-char key |
| `fobFlashed` | `boolean` | Whether fob provisioned |
| `receiverFlashed` | `boolean` | Whether receiver provisioned |
| `currentStepIdx` | `number` | Keys flow step (0-3) |
| `keysProvisioningInProgress` | `boolean` | Operation lock |

**Helper functions:**

- **`animateHeightChange(callback)`** - Animates panel height transition (400ms CSS).
- **`abortableDelay(ms, shouldAbort) -> Promise`** - Cancellable timer.
- **`triggerConfetti()`** - Fires canvas-confetti particles from both sides.
- **`runTimeout(containerEl, circleEl, ms, shouldAbort)`** - SVG ring countdown animation.

**Keys flow functions:**

- **`setKey(key)`** - Stores key and enables/disables flash buttons.

- **`showStep(stepIndex, pushState)`** - Transitions between steps 0-3.

- **`animateKeyGeneration(finalKey)`** - Animated key reveal: fade, scramble effect, blur dots, ring timeout, advance to step 1.

- **`provisionDevice(deviceId, setStatus, expectedBooted)`** - Full provisioning sequence: request port, open connection, send PROV line, wait for ACK, wait for boot message, mark device flashed, auto-advance.

- **`resetKeysUI()`** - Resets all keys flow state to step 0.

**Keys flow steps:**

| Step | Name | Description |
|------|------|-------------|
| 0 | Generate | Generate button visible |
| 1 | Flash Fob | Flash fob button + stepper |
| 2 | Flash Receiver | Flash receiver button + stepper |
| 3 | Complete | Success notes + confetti |

---

# 5. BLE Protocol Specification

## 5.1 Advertisement Format

**Manufacturer-Specific Data (MSD): 15 bytes total**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0-1 | 2 | Company ID | Little-endian, default `0xFFFF` |
| 2-5 | 4 | Counter | Little-endian, monotonically increasing |
| 6 | 1 | Command | `0x01` = Unlock, `0x02` = Lock |
| 7-14 | 8 | MIC | AES-128-CCM authentication tag |

## 5.2 AES-128-CCM Parameters

| Parameter | Value |
|-----------|-------|
| Key size | 128 bits (16 bytes) |
| Nonce | 13 bytes: counter_LE(4) + zeros(9) |
| Message | 5 bytes: counter_LE(4) + command(1) |
| Tag length (M) | 8 bytes |
| Length field (L) | 2 bytes |
| Hardware | nRF52840 SoftDevice ECB engine |

## 5.3 Provisioning Protocol

**Format:** `PROV:<key_32_hex>:<counter_8_hex>:<checksum_4_hex>\n`

**Responses:**

| Response | Meaning |
|----------|---------|
| `ACK:PROV_SUCCESS` | Key stored successfully |
| `ERR:MALFORMED` | Invalid format |
| `ERR:CHECKSUM` | CRC-16-CCITT mismatch |
| `ERR:STORAGE` | Flash write failed |

**Checksum:** CRC-16-CCITT (polynomial 0x1021, init 0xFFFF) over 16 key bytes.

---

# 6. Security Architecture

| Feature | Implementation |
|---------|---------------|
| Authentication | AES-128-CCM with 8-byte MIC |
| Replay prevention | Monotonic counter, persistent log with CRC-32 integrity |
| Timing attack resistance | Constant-time MAC comparison |
| Key provisioning | USB-only (physical access required), CRC-16 validated |
| Key storage | Internal flash filesystem (not readable via BLE) |
| Power-loss resilience | CRC-32 per counter record, skip corrupted entries |
| Low power | System OFF mode (<1 uA), event-driven BLE scanning |

---

# 7. External Dependencies

## Firmware (Guillemot & Uguisu)

| Dependency | Purpose |
|------------|---------|
| Arduino framework | Core platform |
| Bluefruit52_Arduino | BLE stack for nRF52 |
| Adafruit TinyUSB Library | USB serial for provisioning |
| Adafruit_LittleFS | Flash filesystem |
| Nordic SoftDevice | BLE stack + hardware AES-ECB |

## Whimbrel (Web App)

| Dependency | Purpose |
|------------|---------|
| Web Serial API | USB CDC communication |
| Web Crypto API | CSPRNG key generation |
| JSZip | Firmware ZIP parsing |
| canvas-confetti | Success animation |
| GitHub API | Firmware release fetching |

**Browser support:** Chrome 89+, Edge 89+, Opera 75+ (Web Serial API required).
