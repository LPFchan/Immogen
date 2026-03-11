# Pipit Master Architecture (Phase 1)

*Date: 2026-03-10*

**Status:** Finalized Master Architecture
**Scope:** Complete architectural blueprint for the Pipit companion app, Guillemot firmware integration, Whimbrel dashboard, and the BLE security protocol. This document serves as the single source of truth, superseding logs 30, 31, and 32.

---

## 1. Overview & Tech Stack

**Pipit** is the companion app for the Immogen immobilizer ecosystem. It provides:
1. **Proximity Unlock (Background):** Low-power background BLE service that detects the vehicle and automatically unlocks Guillemot based on RSSI thresholding.
2. **Active Key Fob (Foreground):** Manual lock/unlock UI, functionally identical to the Uguisu hardware fob.

**Tech Stack:**
* **Kotlin Multiplatform (KMP):** Shared business logic (AES-CCM crypto, state management). Ported from the existing C++ `immo_crypto` logic to pure Kotlin and validated against existing test vectors.
* **Native UI:** **UIKit** (iOS) and **Jetpack Compose** (Android). Native toolkits are used to ensure reliable integration with OS-level BLE background modes, camera (QR scanning), and secure keystores.

---

## 2. Key Slot Architecture

Guillemot supports **4 key slots** (0–3), each with an independent 16-byte AES-CCM PSK, monotonic counter, and an optional 24-character human-readable device name (e.g., "Jamie's iPhone"). 

* **Slot 0:** Strictly reserved for the Uguisu hardware fob.
* **Slots 1–3:** Reserved for Pipit instances (phones).

*Why independent slots?* Sharing a single symmetric key across multiple active devices breaks the monotonic counter system, causing "Leapfrog Desyncs" where the phones continuously reject each other's payloads. Independent slots completely eliminate counter desyncs.

---

## 3. BLE Proximity Architecture

### 3.1 Flipped Roles & iBeacon Wake
Because iOS background advertising is broken (CoreBluetooth strips Manufacturer Data and hashes Service UUIDs into a proprietary format), the BLE roles are flipped from the Uguisu model:
* **Guillemot (Advertiser):** Broadcasts a continuous 500ms beacon as a connectable GATT peripheral.
* **Pipit (Scanner):** Registers a background scanner filtering for the Immogen Proximity UUID.

**iOS Background Optimization:** CoreBluetooth background scanning is throttled to ~3–4 minute intervals, which is unacceptable for proximity unlock. To mitigate this, Guillemot simultaneously broadcasts an **iBeacon** advertisement using a dedicated Immogen Proximity UUID (`66962B67-9C59-4D83-9101-AC0C9CCA2B12`). iOS CoreLocation monitors this beacon region via a hardware-level path, waking Pipit within ~1–2 seconds of approach. The nRF52840's extended advertising support allows both iBeacon and GATT advertisements to broadcast concurrently without alternation.

*   **iOS flow:** CoreLocation detects iBeacon region entry → wakes Pipit → Pipit initiates CoreBluetooth GATT connection → evaluates RSSI → sends payload.
*   **Android flow:** Foreground Service scans directly for the GATT advertisement (no iBeacon needed; Android background scanning is unrestricted).

**iOS Permission Requirement:** iBeacon region monitoring requires **"Always Allow" location permission**. Pipit must request this during onboarding. Users who decline are degraded to the slow CoreBluetooth background scanning path (~3–4 minute intervals). This trade-off should be communicated clearly in the permission prompt.

### 3.2 Stateful Proximity Beaconing
To support both "approach-to-unlock" and "walk-away-to-lock" without causing severe battery drain on the phone while riding, Guillemot dynamically changes its advertised Service UUID based on the latch state:

| State Beacon | UUID |
|---|---|
| **Immogen Proximity - Locked** | `C5380EF2-C3FC-4F2A-B3CC-D51A08EF5FA9` |
| **Immogen Proximity - Unlocked** | `A1AA4F79-B490-44D2-A7E1-8A03422243A1` |

*   **Approach (When Locked):** Guillemot advertises the Locked UUID. Pipit scans for this UUID. When detected, Pipit evaluates the RSSI against the unlock proximity threshold. If strong enough, it connects via GATT and sends the Unlock payload.
*   **Walk-Away (When Unlocked):** Guillemot switches to the Unlocked UUID. Pipit stops scanning for the Locked UUID (preventing redundant wakeups) and monitors the Unlocked UUID. When the RSSI drops below the lock proximity threshold (or the beacon drops entirely), it connects and sends the Lock payload.

### 3.3 RSSI Thresholds & Hysteresis
To prevent rapid lock/unlock cycling when the user hovers near the proximity boundary, the unlock and lock thresholds are separated by a 10 dBm hysteresis gap:
*   **Unlock threshold (default):** **-65 dBm** — phone must be close (~1–2 meters) to trigger unlock.
*   **Lock threshold (default):** **-75 dBm** — phone must move significantly farther away (~4–5 meters) before re-locking.

Both thresholds are user-configurable in Pipit's settings. The 10 dBm gap ensures that once unlocked, minor RSSI fluctuations from body movement, pocket placement, or multipath reflections do not trigger spurious re-locks.

---

## 4. GATT & Payload Protocols

### 4.1 GATT Service & Characteristic UUIDs

| Component | UUID |
|---|---|
| **Immogen Proximity Service** | `942C7A1E-362E-4676-A22F-39130FAF2272` |
| Unlock/Lock Command Characteristic | `2522DA08-9E21-47DB-A834-22B7267E178B` |
| Management Command Characteristic | `438C5641-3825-40BE-80A8-97BC261E0EE9` |
| Management Response Characteristic | `DA43E428-803C-401B-9915-4C1529F453B1` |

### 4.2 GATT Characteristic Structure
Guillemot's `Immogen Proximity` GATT service exposes three characteristics:

1.  **Unlock/Lock Command (Write Without Response):** Receives the 14-byte encrypted AES-CCM payload. Like the Uguisu hardware fob, this is a "fire-and-forget" blind write. No custom GATT error feedback is required.
2.  **Management Command (Write, Authenticated Link):** Gated by the 6-digit BLE Pairing PIN. Receives administrative commands (e.g., `PROV`, `REVOKE`, `RENAME`, `SLOTS?`).
3.  **Management Response (Notify):** Returns asynchronous responses to management commands. All responses are structured JSON (see Section 6).

**MTU Negotiation:** Management commands (e.g., `PROV` with a 32-character hex key + name) can exceed the default 23-byte ATT MTU. Guillemot requests an MTU exchange to 128 bytes upon connection. Pipit and Whimbrel (Web Bluetooth) must also request a larger MTU before sending management commands.

### 4.3 Payload Structure & Slot Identification
The 14-byte command payload structure is: `[1-byte Prefix (AAD)] [4-byte Counter (AAD)] [1-byte Command (Ciphertext)] [8-byte MIC]`.

The 1-byte Prefix carries only the Slot ID to route payloads to the correct AES key without brute-forcing decryption across all slots. The Command remains encrypted in the ciphertext to preserve confidentiality.
*   **`Prefix = (Slot_ID << 4)`**
*   **Upper 4 bits:** Target Key Slot (0-3).
*   **Lower 4 bits:** Reserved (zero).

*Example:* `0x10` instructs Guillemot to pull the AES key for Slot 1. The actual command (Unlock/Lock) is only revealed after successful AES-CCM decryption.

---

## 5. Security & Management PIN

### 5.1 The 6-Digit Management PIN
A 6-digit PIN is established during the initial USB-C setup. This PIN serves two roles:
1. **BLE Pairing PIN (SMP):** Used as the standard BLE Pairing PIN for authenticated management sessions.
2. **QR Key Encryption:** Used as the input to an Argon2id KDF to derive an AES-128 key that encrypts the slot key inside provisioning QR codes (see Section 7.1).

### 5.2 Security via BLE Pairing PIN (SMP)
The PIN is not sent as a custom plaintext payload. It acts as the **standard BLE Pairing PIN (SMP)**. When Pipit or Whimbrel attempts to access Management characteristics, the host OS (iOS/Android/Windows) natively prompts the user for the 6-digit PIN. This establishes a fully encrypted and MITM-protected BLE session before any sensitive data (like new AES root keys) is transmitted.

*Because the SoftDevice requires the plaintext PIN to execute the SMP mathematical handshake, the `SETPIN` command transmits and stores the 6 digits in plaintext.*

### 5.3 Brute-Force Rate Limiting
* Exponential backoff after 3 consecutive failures (5s → 10s → 20s → 40s → ...).
* Hard lockout after 10 consecutive failures (BLE management disabled entirely). Recovery requires USB-C serial via Whimbrel (`RESETLOCK` command, serial-only).

### 5.4 Counter Security Model
Each key slot maintains an independent strictly-monotonic counter. Guillemot rejects any payload where `counter <= last_seen_counter` for that slot. There is no tolerance window — this is by design. A single replayed or out-of-order payload is always rejected. This strict model is the core of the anti-replay security and eliminates the complexity of window-based counter acceptance.

---

## 6. Serial & Management Protocol

### 6.1 Command Transport
Commands are accepted over two transports with different permission levels:

| Command | USB-C Serial | BLE GATT (Authenticated) |
|---|---|---|
| `PROV:<slot>:<key>:<ctr>:[name]` | Yes | Yes |
| `RENAME:<slot>:<name>` | Yes | Yes |
| `SLOTS?` | Yes | Yes |
| `REVOKE:<slot>` | Yes | Yes |
| `SETPIN:<6digits>` | Yes | **No** (serial-only) |
| `RESETLOCK` | Yes | **No** (serial-only) |

`SETPIN` and `RESETLOCK` are restricted to USB-C serial because they are recovery/bootstrap operations. Exposing `RESETLOCK` over BLE would allow an attacker to clear brute-force lockout and keep attacking the PIN. `SETPIN` is serial-only to prevent remote PIN changes by a compromised phone.

### 6.2 Response Format
All management commands respond with structured JSON over both transports:

**Success responses:**
```json
{"status":"ok","slot":1,"name":"iPhone","counter":0}
```

**Error responses:**
```json
{"status":"error","code":"MALFORMED","msg":"invalid slot"}
```

**`SLOTS?` response:**
```json
{"status":"ok","slots":[
  {"id":0,"used":true,"counter":4821,"name":"Uguisu"},
  {"id":1,"used":true,"counter":127,"name":"iPhone"},
  {"id":2,"used":false,"counter":0,"name":""},
  {"id":3,"used":false,"counter":0,"name":""}
]}
```

---

## 7. Key Provisioning, Migration, and Recovery

### 7.1 Initial Provisioning (Whimbrel)
1. Flash Guillemot and Uguisu via USB-C.
2. Whimbrel asks user to set the 6-digit PIN (`SETPIN:123456`).
3. Whimbrel provisions Slot 1 (`PROV:1:<key>:0:iPhone`).
4. Whimbrel generates an **encrypted QR code:**
   * Derives an AES-128 key from the PIN using **Argon2id** (parameters: `m=262144` (256 MB), `t=3`, `p=1`, with a random 16-byte salt).
   * Encrypts the slot key using AES-CCM with the derived key.
   * QR contains: `immogen://prov?slot=1&salt=<hex>&ekey=<encrypted_key_hex>&ctr=0&name=iPhone`
   * The PIN is **never** included in the QR code.
5. User scans the QR on their phone. Pipit prompts for the 6-digit PIN, derives the same AES key via Argon2id, decrypts the slot key, and stores it in the platform's secure keystore.

*Security: Even if the QR code is photographed, the slot key cannot be recovered without the 6-digit PIN. Argon2id (256 MB) makes offline brute-force expensive (~800ms per guess on CPU, ~220 hours for 1M combinations; ~12 hours on a 10-GPU cluster).*

### 7.2 The "Migration" Flow (Happy Path)
Used when a user is upgrading to a new phone and has both devices in hand.
1. The old phone generates an **encrypted QR code** containing its current Slot ID, AES Key, and **current Counter value**, encrypted with the user's PIN via Argon2id (same scheme as Section 7.1).
2. The new phone scans the QR, prompts the user for their PIN, decrypts the credentials, and takes over the counter exactly where the old phone left off.
3. The old phone instantly deletes the key from its local secure storage.
*Result: Instant transfer, zero counter desyncs, no BLE management interaction required. QR is safe even if intercepted.*

### 7.3 The "Recovery" Flow (Break-Glass Path)
Used when a phone is lost or destroyed. The user only needs their 6-digit BLE Pairing PIN.
1. The user installs Pipit on a new phone, connects to Guillemot, and authenticates using the PIN.
2. Pipit queries the slots (`SLOTS?`), and Guillemot returns a JSON array.
3. Pipit displays a UI: *"Which device did you lose?"* listing the names (e.g., "Jamie's iPhone").
4. The user selects "Jamie's iPhone" (Slot 1).
5. Pipit issues `REVOKE:1`, instantly locking out the stolen phone.
6. Pipit generates a random 16-byte AES key using the platform's secure random generator (`SecRandomCopyBytes` on iOS, `SecureRandom` on Android) and provisions itself into that vacated slot via `PROV:1:<new_key>:0:New iPhone`.
*Result: Securely locks out the old device and establishes a brand new cryptographic counter baseline.*

---

## 8. Platform Capability Matrix

| Operation | Pipit (Android) | Pipit (iOS) | Whimbrel (laptop) |
|---|---|---|---|
| **Guillemot firmware flash** | USB OTG (`.uf2` via `libaums`) | Not supported | USB-C DFU |
| **Uguisu firmware/key flash** | USB OTG (CDC serial via `usb-serial-for-android`) | Not supported | Web Serial |
| **Key management (BLE)** | BLE GATT + OS PIN Prompt | BLE GATT + OS PIN Prompt | Web Bluetooth + OS PIN Prompt |
| **Phone provisioning** | Encrypted QR scan + PIN entry | Encrypted QR scan + PIN entry | Web Bluetooth + Encrypted QR display |
| **Proximity unlock** | Background scan (Foreground Service) | iBeacon region monitoring (CoreLocation) + GATT | N/A |
| **Active key fob** | BLE GATT write | BLE GATT write | N/A |

---

## 9. Required Codebase Modifications

Implementing this architecture requires targeted updates across all four projects in the monorepo:

### 9.1 `ImmoCommon` (Shared Library)
*   **Struct Update (`immo_storage.h`):** Refactor the storage struct from a single key/counter into an array of 4 Key Slots. Each slot must contain an AES key array, a monotonic counter, and a `char name[24]` buffer. A clean reflash is acceptable for the storage migration (prototyping phase).
*   **Crypto Update (`immo_crypto.cpp`):** Update `verify_payload()` to extract the `Slot ID` using bitwise logic (`prefix >> 4`), validate that the target slot is active, and fetch the correct AES key from storage before executing the CCM MIC check.

### 9.2 `Guillemot` (Immobilizer Firmware)
*   **BLE Initialization (`main.cpp`):** Initialize dual BLE roles (`Central` + `Peripheral`).
*   **Stateful Beaconing:** Implement advertising logic to dynamically swap the Service UUID between `Locked` and `Unlocked` based on the latch state.
*   **GATT Server Setup:** Define the `Immogen Proximity` service. Add the `Unlock Command` (Write Without Response), `Management Command` (Write), and `Management Response` (Notify) characteristics.
*   **Security Manager (SMP):** Configure the SoftDevice security manager to require an Authenticated Link for the Management characteristics. Feed the plaintext 6-digit PIN from flash into the `ble_gap_opt_passkey_t` structure.
*   **iBeacon Advertising:** Add a concurrent iBeacon advertising set using the Immogen Proximity UUID (`66962B67-9C59-4D83-9101-AC0C9CCA2B12`) via the nRF52840's extended advertising support.
*   **Parser Expansion:** Update the serial parser to handle the new `RENAME` command, accept names in the `PROV` command, format `SLOTS?` output as JSON, and accept 6 plaintext digits for `SETPIN`. Route incoming GATT `Management Command` writes into this same parser. The parser must gate `SETPIN` and `RESETLOCK` to serial-only; reject these commands when the source is GATT (see Section 6.1).

### 9.3 `Uguisu` (Hardware Fob Firmware)
*   **Prefix Byte Packing:** Update the payload builder in `Uguisu/firmware/src/main.cpp` to explicitly pack `0x00` (Slot 0) into the upper 4 bits of the prefix byte when broadcasting, ensuring Guillemot routes the payload to the reserved hardware fob slot.

### 9.4 `Whimbrel` (Web Dashboard)
*   **Protocol Updates (`serial.js` / `api.js`):** Update the API wrappers to append device names to the `PROV` command, implement the `RENAME` command, and send `SETPIN` as plaintext digits instead of a hash.
*   **BLE Management (`js/`):** Implement Web Bluetooth to connect to the new GATT service, request access to the `Management Command` characteristic (triggering the browser's native PIN prompt), and listen to `Management Response` notifications.
*   **JSON Parsing:** Parse the JSON response from `SLOTS?` to populate the dashboard UI with slot IDs, usage status, and device names.
*   **UI & QR Updates (`app.js` / `prov.js`):** Add text fields for Device Name during the "Add Phone" wizard. Add an "Edit Name" button next to active slots in the dashboard view. Implement Argon2id KDF + AES-CCM encryption for QR code generation. The QR payload must contain the salt, encrypted key, counter, slot, and name — but never the PIN.