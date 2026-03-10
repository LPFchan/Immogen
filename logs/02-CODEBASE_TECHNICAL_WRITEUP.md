# BLE Immobilizer System — Complete Technical Writeup

*Date: 2026-03-09 06:13*

**Ninebot G30 Immobilizer: Guillemot, Uguisu, ImmoCommon & Whimbrel**

This document provides a human-readable, module-by-module technical reference for every component of the BLE-based immobilizer system.

---

## System Overview

The system consists of four integrated components:

| Component | Role | Platform |
|-----------|------|----------|
| **Uguisu** | BLE key fob — broadcasts encrypted advertisements on button press | XIAO nRF52840 (embedded) |
| **Guillemot** | Deck receiver — scans for adverts, validates MIC, controls power latch | XIAO nRF52840 (embedded) |
| **ImmoCommon** | Shared C++ library — crypto, provisioning, storage | nRF52840 firmware |
| **Whimbrel** | Web app — key provisioning + firmware flashing via Web Serial | Browser (Chrome/Edge) |

**Flow:** Uguisu button press → encrypted BLE advert → Guillemot receives → validates AES-CCM MIC → SET/RESET SR latch → gates battery-to-ESC power.

---

# Part 1: ImmoCommon

Shared library used by both Uguisu and Guillemot. Provides cryptography, provisioning, and storage.

**Location:** `ImmoCommon/` (or `lib/ImmoCommon` as submodule in firmware projects)

---

## 1.1 Module: `immo_crypto`

**Files:** `src/immo_crypto.h`, `src/immo_crypto.cpp`

### Constants
| Name | Value | Purpose |
|------|-------|---------|
| `MIC_LEN` | 8 | AES-CCM authentication tag length (bytes) |
| `MSG_LEN` | 5 | Plaintext message: counter(4) + command(1) |
| `PAYLOAD_LEN` | 13 | Full payload: msg(5) + mic(8) |
| `NONCE_LEN` | 13 | Nonce: counter(4) + 9 zero bytes |

### Types
- **`Command`** (enum): `Unlock = 0x01`, `Lock = 0x02`
- **`Payload`** (struct): `counter` (uint32_t), `command` (Command), `mic[8]`

### Functions

| Function | Signature | Purpose |
|----------|-----------|---------|
| `build_nonce` | `void build_nonce(uint32_t counter, uint8_t nonce[NONCE_LEN])` | Builds 13-byte nonce: LE32 counter + 9 zeros |
| `build_msg` | `void build_msg(uint32_t counter, Command command, uint8_t msg[MSG_LEN])` | Builds 5-byte message: LE32 counter + command byte |
| `ccm_mic_8` | `bool ccm_mic_8(key, nonce, msg, msg_len, out_mic)` | Computes AES-128-CCM 8-byte MIC over message |
| `constant_time_eq` | `bool constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n)` | Constant-time byte comparison (timing-attack resistant) |

### Internal (anonymous namespace)
- **`le16_write`** / **`le32_write`**: Little-endian byte serialization
- **`aes128_ecb_encrypt`**: Uses nRF `sd_ecb_block_encrypt` for AES-128-ECB
- **`xor_block`**: XORs two 16-byte blocks into destination

### CCM Implementation
- L=2, M=8 (flags in B0)
- CBC-MAC over B0 || message blocks, then encrypt S0, XOR with X for tag

---

## 1.2 Module: `immo_provisioning`

**Files:** `src/immo_provisioning.h`, `src/immo_provisioning.cpp`

### Functions

| Function | Signature | Purpose |
|----------|-----------|---------|
| `prov_is_vbus_present` | `bool prov_is_vbus_present()` | Returns true if USB VBUS detected (nRF52840/33: `NRF_POWER->USBREGSTATUS`) |
| `prov_run_serial_loop` | `bool prov_run_serial_loop(timeout_ms, on_success)` | Waits for `PROV:` line on Serial; parses key/counter; validates CRC-16; calls `on_success` if valid |
| `ensure_provisioned` | `void ensure_provisioned(timeout_ms, on_success, load_provisioning, is_provisioned)` | If VBUS: runs loop once. If not provisioned and VBUS still present, loops indefinitely until success |

### PROV Protocol
- **Format:** `PROV:<key_32_hex>:<counter_8_hex>:<checksum_4_hex>`
- **Checksum:** CRC-16-CCITT over 16-byte key
- **Responses:** `ACK:PROV_SUCCESS`, `ERR:MALFORMED`, `ERR:CHECKSUM`, `ERR:STORAGE`

### Internal Helpers
- **`hex_byte`**: Parses 2 hex chars → uint8_t
- **`parse_counter_hex`**: Parses 8 hex chars → uint32_t
- **`crc16_ccitt`**: Standard CRC-16-CCITT (polynomial 0x1021, init 0xFFFF)

---

## 1.3 Module: `immo_storage`

**Files:** `src/immo_storage.h`, `src/immo_storage.cpp`

### Types
- **`CounterRecord`**: `{ counter (uint32_t), crc32 (uint32_t) }`

### Class: `CounterStore`

| Method | Purpose |
|--------|---------|
| `CounterStore(log_path, old_log_path, max_bytes)` | Constructor; paths for main and rotated log |
| `begin()` | Initializes Adafruit `InternalFS` |
| `load()` | Scans log file, validates CRC per record, sets `last_counter_` |
| `lastCounter()` | Returns last seen counter |
| `loadLast()` | Calls `load()` then returns `lastCounter()` |
| `update(counter)` | Appends new record; rotates log if `max_bytes` exceeded |
| `seed(counter)` | Removes old logs, writes single record (provisioning reset) |

### Internal
- **`crc32_ieee`**: IEEE CRC-32 over data
- **`record_crc`**: CRC-32 of 4-byte counter for record integrity
- **`rotateIfNeeded_`**: Renames log → old, clears for new append

---

## 1.4 Module: `immo_tusb_config`

**File:** `src/immo_tusb_config.h`

- Includes Adafruit nRF TinyUSB config
- **Disables Mass Storage Class** (`CFG_TUD_MSC`, `CFG_TUH_MSC` = 0) to avoid `File` class collision with Adafruit_LittleFS

---

## 1.5 Header: `ImmoCommon.h`

**File:** `src/ImmoCommon.h`

Single include: pulls in `immo_crypto.h`, `immo_provisioning.h`, `immo_storage.h`.

---

## 1.6 Tool: `gen_mic.py`

**File:** `tools/test_vectors/gen_mic.py`

**Purpose:** Generates AES-128-CCM MIC test vectors for firmware validation.

**Usage:**
```bash
python3 gen_mic.py --counter <int> --command <0x01|0x02> --key <32_hex_chars> [--company-id 0xFFFF]
```

**Outputs:** nonce, msg, mic, payload_13B, msd_company_plus_payload (15 bytes)

---

# Part 2: Guillemot (Deck Receiver)

**Location:** `Guillemot/`  
**Firmware:** `Guillemot/firmware/guillemot/`

---

## 2.1 Configuration

**Files:** `include/guillemot_config.h` (includes `guillemot_config.example.h`)

| Constant | Value | Purpose |
|----------|-------|---------|
| `PIN_LATCH_SET` | D0 | SR latch SET pin |
| `PIN_LATCH_RESET` | D1 | SR latch RESET pin |
| `PIN_BUZZER` | D3 | Buzzer PWM pin |
| `SCAN_INTERVAL_MS` | 2000 | BLE scan interval |
| `SCAN_WINDOW_MS` | 20 | Scan window (duty cycle) |
| `MSD_COMPANY_ID` | 0xFFFF | BLE MSD company ID |
| `BUZZER_HZ` | 4000 | Buzzer frequency |
| `BUZZER_UNLOCK_MS` | 120 | Unlock tone duration |
| `BUZZER_LOCK_MS` | 200 | Lock tone duration |
| `LATCH_PULSE_MS` | 15 | SET/RESET pulse width |
| `PIN_ERROR_LED` | 26 | Error blink LED (P0.26) |

---

## 2.2 Main Application

**File:** `src/main.cpp`

### Global State
- `g_psk[16]`: Runtime AES key (from flash or zeros)
- `g_store`: `CounterStore` for replay counter
- Paths: `/ctr.log`, `/ctr.old`, `/psk.bin`
- `COUNTER_LOG_MAX_BYTES`: 4096
- `PROV_TIMEOUT_MS`: 30000

### Functions

| Function | Purpose |
|----------|---------|
| `on_provision_success` | Writes key to `/psk.bin`, seeds counter store, updates `g_psk` |
| `key_is_all_zeros` | Returns true if `g_psk` is all zeros |
| `load_psk_from_storage` | Loads key from `/psk.bin` or defaults to zeros |
| `latch_set_pulse` | SET pulse: RESET LOW, SET HIGH, delay, SET LOW |
| `latch_reset_pulse` | RESET pulse: SET LOW, RESET HIGH, delay, RESET LOW |
| `buzzer_tone_ms` | `tone()` for duration, then `noTone()` |
| `led_error_loop` | Blinks error LED forever (InternalFS failure) |
| `parse_payload_from_report` | Extracts MSD from BLE report; checks company ID; fills `Payload` |
| `verify_payload` | Recomputes MIC, compares with `constant_time_eq` |
| `handle_valid_command` | Anti-replay check (counter > last). Unlock: latch SET, buzzer, update. Lock: buzzer, latch RESET, update |
| `scan_callback` | BLE scanner callback: parse → verify → handle |

### Setup Flow
1. Init GPIO (latch, buzzer)
2. Serial @ 115200
3. `g_store.begin()` — InternalFS
4. `load_psk_from_storage`
5. `immo::ensure_provisioned` (USB provisioning if VBUS)
6. `g_store.load`
7. Bluefruit init, Scanner start (filter MSD by company ID)
8. Print "Guillemot scanning", "BOOTED:Guillemot"

### Loop
`sd_app_evt_wait()` — power-efficient sleep

---

## 2.3 Build

**File:** `platformio.ini`

- **Board:** `xiaoble_adafruit` (XIAO nRF52840)
- **Libs:** Bluefruit52_Arduino, Adafruit TinyUSB, SPI
- **Flags:** `CFG_TUSB_CONFIG_FILE`, `-I include`, `-I lib/ImmoCommon/src`

---

# Part 3: Uguisu (Key Fob)

**Location:** `Uguisu/`  
**Firmware:** `Uguisu/firmware/uguisu/`

---

## 3.1 Configuration

**Files:** `include/uguisu_config.h` (includes `uguisu_config.example.h`)

| Constant | Value | Purpose |
|----------|-------|---------|
| `MSD_COMPANY_ID` | 0xFFFF | BLE MSD company ID |
| `UGUISU_PIN_BUTTON` | D0 | Button pin |
| `UGUISU_PIN_BUTTON_NRF` | 2 | P0.2 for wake-from-system-off |
| `PIN_LED_B/R/G` | D1/D2/D3 | RGB LED (active-low) |
| `UGUISU_ADVERTISE_MS` | 2000 | Advertise duration |
| `UGUISU_ADV_INTERVAL_MS` | 100 | Interval between adverts |
| `UGUISU_LONG_PRESS_MS` | 1000 | Long press = Lock |
| `UGUISU_BUTTON_TIMEOUT_MS` | 10000 | Wait for press before sleep |
| `PIN_ERROR_LED` | 26 | Error blink |

---

## 3.2 Main Application

**File:** `src/main.cpp`

### Global State
- `g_psk[16]`: Runtime key
- `g_store`: `CounterStore` (`/ug_ctr.log`, `/ug_ctr.old`)
- `PROV_STORAGE_PATH`: `/ug_prov.bin` (4-byte magic + 16-byte key)
- `PROV_MAGIC`: 0x76704755 ("UGpv" LE)

### Functions

| Function | Purpose |
|----------|---------|
| `on_provision_success` | Writes magic+key to `/ug_prov.bin`, seeds store, updates `g_psk` |
| `key_is_all_zeros` | Check if unprovisioned |
| `load_provisioning` | Load key from `/ug_prov.bin` if valid |
| `start_advertising_once` | Builds MSD (company_id + payload), starts BLE advert |
| `system_off` | Stops BLE, configures GPIO sense for wake, `sd_power_system_off()` |
| `wait_for_button_press_release` | Waits for press and release; returns press duration (ms) |
| `led_error_loop` | Error blink loop |

### Setup Flow
1. Serial @ 115200, button INPUT_PULLUP
2. `g_store.begin()`, `load_provisioning`
3. `immo::ensure_provisioned` (USB mode if VBUS)
4. Print "BOOTED:Uguisu"
5. Bluefruit init
6. Wait for button (short = Unlock, long = Lock)
7. If no press: `system_off()`
8. Get counter = `loadLast() + 1`
9. Build nonce, msg, compute MIC via `ccm_mic_8`
10. Build payload, `start_advertising_once`
11. `delay(UGUISU_ADVERTISE_MS)`, `g_store.update`, `system_off`

### Loop
Empty — device sleeps after each use.

---

## 3.3 Build

**File:** `platformio.ini`

Same as Guillemot (XIAO nRF52840, Bluefruit, TinyUSB, ImmoCommon). Flags: `UGUISU_BUILD_PIO`, `CFG_TUSB_CONFIG_FILE`, `-I lib/ImmoCommon/src`.

---

# Part 4: Whimbrel (Web App)

**Location:** `Whimbrel/`  
Browser-based provisioning and firmware flashing.

---

## 4.1 Structure

```
Whimbrel/
├── index.html
├── manifest.json
├── css/style.css
├── js/
│   ├── app.js          # Main UI, Keys flow
│   ├── config.js       # Constants
│   ├── crypto.js       # Key gen, CRC, PROV line
│   ├── serial.js       # Web Serial helpers
│   ├── prov.js         # Boot wait
│   ├── firmware.js     # Firmware tab controller
│   ├── api.js          # GitHub releases
│   ├── firmware-manager.js  # ZIP download/parse
│   └── dfu.js          # Nordic Serial DFU
```

---

## 4.2 Module: `config.js`

| Key | Value | Purpose |
|-----|-------|---------|
| `GITHUB_OWNER` | "LPFchan" | GitHub org for releases |
| `BAUDRATE` | 115200 | Serial baud |
| `KEY_LEN_BYTES` | 16 | AES key size |
| `RESET_COUNTER` | "00000000" | Counter in PROV line |
| `DEVICE_ID_FOB` | "UGUISU_01" | Fob identifier |
| `DEVICE_ID_RX` | "GUILLEMOT_01" | Receiver identifier |
| `BOOTED_FOB` | "BOOTED:Uguisu" | Expected fob boot string |
| `BOOTED_RX` | "BOOTED:Guillemot" | Expected receiver boot string |
| `TIMEOUT_PROV_MS` | 12000 | Provisioning response timeout |
| `TIMEOUT_BOOT_MS` | 10000 | Boot confirmation timeout |
| `DFU_DEFAULT_MTU` | 256 | DFU MTU |
| `DFU_BUFFER_SIZE` | 8192 | Serial buffer |

---

## 4.3 Module: `crypto.js`

| Function | Purpose |
|----------|---------|
| `generateKey()` | `crypto.getRandomValues` 16 bytes → 32 hex chars |
| `crc16Key(keyHex)` | CRC-16-CCITT over key bytes (matches ImmoCommon) |
| `buildProvLine(keyHex)` | Returns `PROV:<key>:<counter>:<checksum>` |

---

## 4.4 Module: `serial.js`

| Export | Purpose |
|--------|---------|
| `isSupported()` | `"serial" in navigator` |
| `requestPort()` | `navigator.serial.requestPort()` |
| `SerialConnection` | Wraps Web Serial: open, sendLine, readLineWithTimeout, close. Uses TextEncoderStream/TextDecoderStream. Reader loop processes newline-delimited lines, resolves pending promises. |

### SerialConnection Methods
- `open(port, options)` — Opens at baudRate, sets up writer/reader
- `sendLine(line)` — Writes line + "\n"
- `readLineWithTimeout(timeoutMs)` — Returns first complete line or rejects on timeout
- `close()` — Cancels reader, closes port

---

## 4.5 Module: `prov.js`

| Function | Purpose |
|----------|---------|
| `waitForBooted(serialConnection, expectedBooted)` | Reads lines until `expectedBooted` (e.g. "BOOTED:Uguisu") or throws on ERR/timeout |

---

## 4.6 Module: `api.js`

| Function | Purpose |
|----------|---------|
| `fetchDeviceReleases(repoName)` | Fetches `https://api.github.com/repos/LPFchan/<repo>/releases`, filters to releases with .zip assets |

---

## 4.7 Module: `firmware-manager.js`

| Function | Purpose |
|----------|---------|
| `fetchAndParseFirmwareZip(zipUrl, arrayBufferCache)` | Downloads ZIP (or uses cache), parses manifest.json, extracts .dat and .bin from application manifest |

---

## 4.8 Module: `dfu.js`

### SlipFramer
- **`append(chunk)`**: SLIP decode; returns array of decoded packets
- **`SlipFramer.encode(packet)`**: SLIP encode (0xC0, 0xDB/0xDC, 0xDB/0xDD escaping)

### DfuFlasher
Nordic Serial DFU over Web Serial. Uses SLIP framing.

**Opcodes:** PROTOCOL_VERSION, CREATE_OBJECT, SET_PRN, CALC_CHECKSUM, EXECUTE, SELECT_OBJECT, MTU_GET, WRITE_OBJECT, PING, RESPONSE (0x60)

**Object types:** COMMAND (0x01), DATA (0x02)

**Methods:**
- `ping()` — OP_PING
- `setPRN(prn)` — Set Packet Receipt Notification
- `getMTU()` — Get MTU
- `selectObject(type)` — Returns maxSize, offset, crc
- `createObject(type, size)` — Create object
- `writeObject(dataChunk)` — Write in chunks (MTU/2 - 2)
- `calcChecksum()` — Returns offset, crc
- `execute()` — Execute object
- `flash(onProgress)` — Full flash: ping, setPRN(0), getMTU, write command (.dat), execute, write data (.bin) in chunks, execute each

**CRC32:** Standard IEEE table-based CRC-32 for validation.

---

## 4.9 Module: `firmware.js`

**`initFirmwareTab(opts)`** — Returns: `showFwStep`, `resetFwFlashUI`, `handleFirmwarePopState`, `abortFwFlash`, `isFwFlashing`, `getFwStepIdx`

### Steps
1. **Step 0:** Device tiles (Guillemot / Uguisu)
2. **Step 1:** DFU instructions (connect USB, double-tap reset, drive appears)
3. **Step 2:** Release dropdown, Flash button, progress bar

### Flow
- `fetchReleases(repoName)` — Fetches GitHub releases, populates dropdown
- `selectRelease(releaseData)` — Sets `latestFwZipUrl`
- Flash: `requestPort` → `fetchAndParseFirmwareZip` → `DfuFlasher.flash` → success + confetti

---

## 4.10 Module: `app.js`

Main UI orchestration. Imports: config, crypto (generateKey, buildProvLine), serial, prov (waitForBooted), firmware (initFirmwareTab).

### Keys Tab Flow (4 steps)
0. **Generate:** Generate key, animated key reveal, 1.5s timer → step 1
1. **Flash Fob:** provisionDevice → send PROV line → wait ACK → waitForBooted → 1.5s → step 2
2. **Flash Receiver:** Same provision flow
3. **Done:** Confetti, "Unplug both devices"

### Helpers
- `animateHeightChange(callback)` — Smooth panel height transition
- `abortableDelay(ms, shouldAbort)` — Delay that can be aborted
- `triggerConfetti()` — canvas-confetti animation
- `provisionDevice(deviceId, setStatus, expectedBooted)` — Serial open → buildProvLine → sendLine → readLineWithTimeout → waitForBooted

### State
- `currentKey`, `fobFlashed`, `receiverFlashed`
- `currentStepIdx` (0–3)
- Tab switching, popstate handling, theme toggle

---

## 4.11 HTML & Manifest

**index.html:** Header (Whimbrel, GitHub link), unsupported msg, tabs (Firmware / Keys), main panel with Keys/Firmware sections, footer theme toggle. Loads confetti, JSZip, app.js.

**manifest.json:** PWA manifest; name "Whimbrel", start_url, standalone display.

---

# Summary: Data Flow

1. **Provisioning:** Whimbrel generates key → `buildProvLine` → Serial `PROV:...` → device `prov_run_serial_loop` → CRC check → `on_provision_success` → key + counter stored in flash.
2. **Unlock:** Uguisu button → counter++ → `build_nonce`, `build_msg`, `ccm_mic_8` → BLE advert with MSD → Guillemot scan → parse → verify MIC → counter > last → latch SET → power on.
3. **Lock:** Same, command 0x02 → latch RESET → power off.
4. **Firmware:** Whimbrel fetches .zip from GitHub → parse manifest → DfuFlasher over Web Serial (Nordic DFU) → flash .dat + .bin.

---

*End of technical writeup.*
