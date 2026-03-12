# Pipit Master Architecture (Phase 1)

*Date: 2026-03-10*

**Status:** Finalized Master Architecture
**Scope:** Complete architectural blueprint for the Pipit companion app, Guillemot firmware integration, Whimbrel dashboard, and the BLE security protocol. This document serves as the single source of truth, superseding logs 30, 31, and 32.

---

## 1. Overview & Tech Stack

**Pipit** is the companion app for the Immogen immobilizer ecosystem. *(Note on Terminology: Throughout this document, the target Ninebot G30 or any future compatible PEV is referred to universally as the **vehicle**, not "scooter").* 

It provides:
1. **Proximity Unlock (Background):** Low-power background BLE service that detects the vehicle and automatically unlocks Guillemot based on RSSI thresholding.
2. **Active Key Fob (Foreground):** Manual lock/unlock UI, functionally identical to the Uguisu hardware fob.

**Tech Stack:**
* **Kotlin Multiplatform (KMP):** Shared business logic (AES-CCM crypto, state management). Ported from the existing C++ `immo_crypto` logic to pure Kotlin and validated against existing test vectors.
* **Native UI:** **UIKit** (iOS) and **Jetpack Compose** (Android). Native toolkits are used to ensure reliable integration with OS-level BLE background modes, camera (QR scanning), and secure keystores.

---

## 2. Key Slot Architecture

Guillemot supports **4 key slots** (0–3), each with an independent 16-byte AES-CCM PSK, monotonic counter, and an optional 24-character human-readable device name (e.g., "Jamie's iPhone"). Slots are divided into three **access tiers** based on slot number:

| Slot | Tier | Default Name | Description |
|---|---|---|---|
| **0** | Hardware | `Uguisu` | Reserved for the Uguisu hardware fob. Lock/unlock only. No BLE management. USB-only administration. |
| **1** | Owner | *(set during onboarding)* | The primary phone. Full management access via SMP. Enforced as the first phone provisioned from Whimbrel. |
| **2** | Guest | `Guest 1` | Lock/unlock + restricted management. No PIN knowledge required. |
| **3** | Guest | `Guest 2` | Lock/unlock + restricted management. No PIN knowledge required. |

The tier is implicit from the slot number — no additional flags or metadata are stored. See Section 5.5 for the access control enforcement model.

*Why independent slots?* Sharing a single symmetric key across multiple active devices breaks the monotonic counter system, causing "Leapfrog Desyncs" where the phones continuously reject each other's payloads. Independent slots completely eliminate counter desyncs.

---

## 3. BLE Proximity Architecture

### 3.1 Flipped Roles & iBeacon Wake
Because iOS background advertising is broken (CoreBluetooth strips Manufacturer Data and hashes Service UUIDs into a proprietary format), the BLE roles are flipped from the Uguisu model:
* **Guillemot (Advertiser):** Broadcasts a continuous beacon as a connectable GATT peripheral. The advertising interval dynamically shifts based on the latch state (see Section 3.2).
* **Pipit (Scanner):** Registers a background scanner filtering for the Immogen Proximity UUID.

**iOS Background Optimization:** CoreBluetooth background scanning is throttled to ~3–4 minute intervals, which is unacceptable for proximity unlock. To mitigate this, Guillemot simultaneously broadcasts an **iBeacon** advertisement using a dedicated Immogen Proximity UUID (`66962B67-9C59-4D83-9101-AC0C9CCA2B12`). The iBeacon broadcasts at a **fixed 300 ms interval**, independent of the latch state. This interval aligns perfectly with Apple's CoreLocation specifications, reliably waking Pipit via a hardware-level path within ~1–2 seconds of approach. The nRF52840's extended advertising support allows both iBeacon and GATT advertisements to broadcast concurrently without alternation.

*   **iOS flow:** CoreLocation detects iBeacon region entry → wakes Pipit → Pipit initiates CoreBluetooth GATT connection → evaluates RSSI → sends payload.
*   **Android flow:** Foreground Service scans directly for the GATT advertisement (no iBeacon needed; Android background scanning is unrestricted).

**iOS Permission Requirement:** iBeacon region monitoring requires **"Always Allow" location permission**. Pipit must request this during onboarding. Users who decline are degraded to the slow CoreBluetooth background scanning path (~3–4 minute intervals). This trade-off should be communicated clearly in the permission prompt.

### 3.2 Stateful Proximity Beaconing & Dynamic Intervals
To support both "approach-to-unlock" and highly-responsive "walk-away-to-lock" without causing severe battery drain on the phone while riding, Guillemot dynamically changes both its advertised Service UUID and its **GATT advertising interval** based on the latch state:

| State Beacon | UUID | Interval | Purpose |
|---|---|---|---|
| **Immogen Proximity - Locked** | `C5380EF2-C3FC-4F2A-B3CC-D51A08EF5FA9` | **300 ms** | Balances battery conservation while parked with relatively fast discovery during approach. |
| **Immogen Proximity - Unlocked** | `A1AA4F79-B490-44D2-A7E1-8A03422243A1` | **200 ms** | Provides high-fidelity, high-frequency RSSI data to the phone to accurately detect walk-aways. |

*   **Approach (When Locked):** Guillemot advertises the Locked UUID at 300 ms. Pipit lazily scans for this UUID. When detected, Pipit evaluates the RSSI against the unlock proximity threshold. If strong enough, it connects via GATT and sends the Unlock payload.
*   **Walk-Away (When Unlocked):** Guillemot switches to the Unlocked UUID at 200 ms. Pipit monitors the Unlocked UUID, maintaining a rolling history of the high-frequency RSSI readings. 
    * **Normal Walk-Away:** When the RSSI drops below the lock proximity threshold (-75 dBm), Pipit connects and sends the Lock payload.
    * **Abrupt Dropout Edge Case:** If the beacon drops entirely *before* reaching the threshold (e.g., walking behind a concrete wall), Pipit analyzes the recent RSSI history. If the trend was decreasing, Pipit infers a walk-away and enters an aggressive scanning state, firing the Lock payload the instant the beacon is visible again. If the trend was stable (e.g., phone reboots mid-ride), it assumes a sudden failure and safely ignores the drop.

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

**MTU Negotiation:** The `SLOTS?` Management Response returns a JSON array containing all 4 slots, which can easily exceed 150 bytes. To accommodate this in a single BLE Notification without fragmentation, Guillemot requests an MTU exchange to **247 bytes** (the nRF52 maximum) upon connection. Note that Web Bluetooth handles MTU negotiation automatically based on the peripheral's request, so Whimbrel does not need an explicit MTU API call.

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

### 5.3 Brute-Force Rate Limiting & DoS Mitigation
* Exponential backoff after 3 consecutive failures (5s → 10s → 20s → 40s → ...).
* **Temporary Lockout:** After 10 consecutive failures, BLE management characteristics are disabled entirely for **1 hour**.
* **Anti-DoS Reset Bypass:** Receiving *any* valid, successfully authenticated AES-CCM Lock or Unlock payload (via the Write Without Response characteristic) proves physical key ownership and **instantly resets the PIN failure counter to zero**, clearing the lockout. This ensures a malicious actor cannot permanently deny the owner BLE management access by spamming bad PINs.

### 5.4 Counter Security Model
Each key slot maintains an independent strictly-monotonic counter. Guillemot rejects any payload where `counter <= last_seen_counter` for that slot. There is no tolerance window — this is by design. A single replayed or out-of-order payload is always rejected. This strict model is the core of the anti-replay security and eliminates the complexity of window-based counter acceptance.

### 5.5 Slot-Based Access Tiers & the `IDENTIFY` Command

Management access is gated by two layers: **SMP authentication** (the phone knows the 6-digit PIN) and **slot identity** (the phone proves which slot it owns). SMP gets you through the door; `IDENTIFY` determines what you can do inside.

**The `IDENTIFY` command:**
At the start of any management session, the phone sends an `IDENTIFY` payload — a standard 14-byte AES-CCM packet (same format as lock/unlock, Section 4.3) with command byte `0x03`. The prefix byte carries the claimed slot ID. Guillemot decrypts the payload using the claimed slot's key:
*   **MIC valid + counter valid →** Session is bound to that slot. Guillemot stores a `session_slot` variable for the lifetime of the GATT connection.
*   **MIC invalid →** Rejected. The session remains unbound and all subsequent management commands are refused.

The `IDENTIFY` payload uses the same counter as lock/unlock (incrementing the slot's monotonic counter). This is acceptable — one counter tick per management session is negligible.

**Unidentified sessions:** If a phone pairs via SMP but never sends `IDENTIFY` (or sends an invalid one), Guillemot refuses all management commands except `SLOTS?` (read-only, no harm). This means a guest phone that only knows the PIN but has no slot key gets no write access even if it manages to pair.

**Permission matrix (enforced in Guillemot's parser after `IDENTIFY`):**

| Command | Slot 1 (Owner) | Slot 2–3 (Guest) | No IDENTIFY (e.g. Lost Phone) |
|---|---|---|---|
| `IDENTIFY` | ✓ | ✓ | N/A |
| `SLOTS?` | ✓ | ✓ | ✓ (read-only fallback) |
| `PROV:<any>` | ✓ | ✗ | ✗ |
| `REVOKE:<any>` | ✓ | ✗ | ✗ |
| `RENAME:<own slot>` | ✓ | ✗ | ✗ |
| `RENAME:<other slot>` | ✓ | ✗ | ✗ |
| `RECOVER:<slot>` | ✗ | ✗ | ✓ *(Only if vehicle is UNLOCKED)* |
| `SETPIN` | serial-only | serial-only | serial-only |
| `RESETLOCK` | serial-only | serial-only | serial-only |

*(Note: The `RECOVER` command allows an unidentified phone to replace a key, but Guillemot strictly enforces that the vehicle's hardware latch must be HIGH/Unlocked for it to succeed. See Section 7.3).*

**Guest access in practice:** Guest phones (Slot 2–3) provision via **unencrypted QR codes** (Section 7.1.1) and never learn the management PIN. They interact exclusively with the Unlock/Lock Command characteristic (Write Without Response, no SMP required). Guests have zero management write access, even to their own slot names.

**Why not skip `IDENTIFY` and just use SMP bonds?** SMP bonds (LTK + IRK) are device-level — they prove "this phone paired before" but not "this phone owns Slot N." IRKs also break when the user clears Bluetooth data, re-pairs, or switches phones. The `IDENTIFY` command provides cryptographic proof of slot ownership that survives re-pairing and is unforgeable without the slot key.

---

## 6. Serial & Management Protocol

### 6.1 Command Transport
Commands are accepted over two transports with different permission levels:

| Command | USB-C Serial | BLE GATT (Authenticated) | Tier Gate |
|---|---|---|---|
| `IDENTIFY` (14-byte AES-CCM payload) | N/A | Yes | Any slot |
| `PROV:<slot>:<key>:<ctr>:[name]` | Yes | Yes | Owner only |
| `RENAME:<slot>:<name>` | Yes | Yes | Owner only |
| `SLOTS?` | Yes | Yes | Any (incl. unidentified) |
| `REVOKE:<slot>` | Yes | Yes | Owner only |
| `RECOVER:<slot>:<key>:<ctr>:[name]` | Yes | Yes | Unidentified (requires Unlocked vehicle) |
| `SETPIN:<6digits>` | Yes | **No** (serial-only) | N/A |
| `RESETLOCK` | Yes | **No** (serial-only) | N/A |

`IDENTIFY` is sent as a 14-byte AES-CCM payload (same format as lock/unlock, command byte `0x03`) through the Management Command characteristic. It binds the BLE session to a slot and access tier (see Section 5.5). Must be sent before any standard write commands; only `SLOTS?` and `RECOVER` are allowed without identification.

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

### 7.1 Initial Provisioning — Owner (Whimbrel)
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

### 7.1.1 Guest Provisioning (Whimbrel or Pipit)
Guest slots (2–3) use **unencrypted QR codes**. The slot key is transmitted in plaintext within the QR payload — no Argon2id, no PIN.

1. The owner provisions a guest slot via Whimbrel (`PROV:2:<key>:0:Guest 1`) or via Pipit (Section 10.6).
2. The provisioning device generates a **plaintext QR code:**
   * QR contains: `immogen://prov?slot=2&key=<hex>&ctr=0&name=Guest%201`
   * Note: `key` field (plaintext) instead of `salt` + `ekey` (encrypted). The presence of `key` vs `ekey` tells the scanning phone whether to prompt for a PIN.
3. The guest scans the QR. Pipit detects the plaintext format, **skips the PIN prompt entirely**, stores the slot key directly in the secure keystore, and completes onboarding.

*Security trade-off: If the QR is photographed or intercepted, the attacker obtains the slot key and can lock/unlock the vehicle. This is accepted for guest slots because (a) QR exchange typically happens in person, (b) the owner can revoke a guest slot at any time, and (c) guest slots have no management privileges. The owner's Slot 1 key is never exposed via this path.*

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
*   **UI & QR Updates (`app.js` / `prov.js`):** Add text fields for Device Name during the "Add Phone" wizard. Add an "Edit Name" button next to active slots in the dashboard view. Implement Argon2id KDF + AES-CCM encryption for QR code generation. The QR payload must contain the salt, encrypted key, counter, slot, and name — but never the PIN. *(Note: Since the native WebCrypto API lacks Argon2id support, Whimbrel requires a WebAssembly port like `argon2-browser` to derive the QR encryption key).*

---

## 10. Pipit UI/UX Architecture

### 10.1 Navigation Model
Pipit uses a **single-screen utility model**. The Home screen (key fob) is always the root view. Settings is the only secondary surface — presented via a **3D flip transition** from the fob model (Section 10.4.2). All management flows (provisioning, migration, slot actions) are reached from within Settings. There are no tab bars, drawers, or deep navigation stacks.

**Theme:** System-follows (respects OS dark/light mode automatically).

### 10.2 Screen Inventory

```
┌──────────────────────────────────────────────────────────────────┐
│                        App Launch                                │
│                            │                                     │
│                 ┌──────────┴──────────┐                          │
│                 ▼                     ▼                          │
│       [No Key in Keystore]    [Key Exists in Keystore]           │
│                 │                     │                          │
│                 ▼                     ▼                          │
│         Onboarding Flow          Home Screen                     │
│         (Camera-first)               │                          │
│                 │              ⚙ flip transition                 │
│                 ▼                     ▼                          │
│            Home Screen           Settings                        │
│                                      ├── Proximity controls      │
│                                      ├── Key Slots (inline)      │
│                                      │    ├── ⋮ Rename           │
│                                      │    ├── ⋮ Replace ──▶ Replace Flow  │
│                                      │    ├── ⋮ Delete           │
│                                      │    └── ⊕ ──▶ Provision New Phone   │
│                                      ├── Transfer to New Phone   │
│                                      ├── USB Flashing (Android)  │
│                                      └── About                   │
└──────────────────────────────────────────────────────────────────┘
```

---

### 10.3 Onboarding Flow (First Launch Only)

Triggered when no AES key exists in the platform secure keystore. The camera opens **immediately** — no welcome preamble.

**Step 1 — QR Scan (Camera-First)**
```
┌─────────────────────────────┐
│                             │
│  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│  ▓▓▓▓▓┌───────────┐▓▓▓▓▓▓  │
│  ▓▓▓▓▓│           │▓▓▓▓▓▓  │
│  ▓▓▓▓▓│  [camera] │▓▓▓▓▓▓  │
│  ▓▓▓▓▓│           │▓▓▓▓▓▓  │
│  ▓▓▓▓▓└───────────┘▓▓▓▓▓▓  │
│  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│  ▓  Scan from Whimbrel    ▓  │
│  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│                             │
│   recover key from lost phone >│
│                             │
└─────────────────────────────┘
```
*   The app launches directly into a **full-screen camera view** with a **darkened overlay** (semi-transparent black, ~70% opacity) covering the entire screen. A **rounded-rectangle transparent window** (QR-sized, centered) cuts through the overlay to form the viewfinder.
*   The instruction text *"Scan from Whimbrel"* sits inside the darkened overlay below the viewfinder.
*   **"recover key from lost phone >"** — a subtle text link at the bottom of the screen. Tapping it launches the Self-Provisioning Recovery Variant of the Replace Flow (Section 10.7).
*   Non-Immogen QR codes (anything not starting with `immogen://prov?`) are **silently ignored** — the viewfinder simply continues scanning. No error toast or inline message.
*   On valid scan → Pipit inspects the QR payload format:
    *   **Encrypted QR** (has `salt` + `ekey` fields — owner/migration): Proceeds to Step 2 (PIN Entry).
    *   **Plaintext QR** (has `key` field — guest provisioning, Section 7.1.1): **Skips PIN entry entirely.** Stores the slot key directly in the secure keystore and jumps straight to the QR Decryption Animation (Step 3, with no KDF wait).

**Step 2 — PIN Entry (Owner Only — Skipped for Guest QR)**
```
┌─────────────────────────────┐
│                             │
│   Enter your 6-digit PIN    │
│                             │
│   This is the PIN you set   │
│   during Guillemot setup.   │
│                             │
│     ┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐    │
│     │ ││ ││ ││ ││ ││ │    │
│     └─┘└─┘└─┘└─┘└─┘└─┘    │
│                             │
│        [ Confirm ]          │
│                             │
└─────────────────────────────┘
```
*   Pipit derives the AES-128 key from the entered PIN via Argon2id (using the salt from the QR payload) and attempts decryption.
*   **Wrong PIN:** Inline error — *"Incorrect PIN. Please try again."* (client-side only; no brute-force concern since the QR is local data).
*   **Correct PIN →** triggers the QR Decryption Animation (Step 3).

**Step 3 — QR Decryption Animation**

On successful PIN entry, a **~1 second visual animation** plays before revealing the result. This mirrors the AES key creation visualization used in Whimbrel.

The animation sequence:
1.  **Dissolve (0–400 ms):** The QR code image (still on screen from Step 1, or re-shown as a thumbnail) breaks apart into a particle field — individual modules scatter outward with randomised velocities and slight rotation. The particles are small squares matching the QR module grid.
2.  **Convergence (400–800 ms):** The scattered particles re-converge toward the center, collapsing into a tight cluster. As they converge, their color shifts from neutral (black/white QR modules) to the app's accent color.
3.  **Resolve (800–1000 ms):** The particle cluster snaps into a key icon (or lock icon) with a brief glow/pulse, signaling "decryption complete". A short haptic fires at the resolve point (`.medium` impact).

The animation is purely decorative — the actual Argon2id KDF + AES-CCM decryption runs concurrently during steps 1–2 and completes well before the animation ends. If KDF takes longer than expected on older hardware, the animation pauses at the convergence phase and holds until decryption finishes.

**Step 4 — Location Permission (iOS Only)**
```
┌─────────────────────────────┐
│                             │
│   Enable proximity unlock?  │
│                             │
│   Pipit can automatically   │
│   unlock your vehicle when  │
│   you walk up to it.        │
│                             │
│   This requires "Always     │
│   Allow" location access    │
│   so the app can detect     │
│   your vehicle in the       │
│   background.               │
│                             │
│   Your location is never    │
│   stored or transmitted.    │
│                             │
│   [ Enable Proximity ]      │
│   [ Skip for Now ]          │
│                             │
└─────────────────────────────┘
```
*   **"Enable Proximity"** → triggers the iOS `CLLocationManager.requestAlwaysAuthorization()` system prompt. On Android, this step is skipped (background scanning uses a Foreground Service with no special location permission).
*   **"Skip for Now"** → proximity unlock is disabled; the user can enable it later from Settings. The app still functions as a manual key fob.

**Step 5 — Done (Slot Overview)**
```
┌─────────────────────────────┐
│                             │
│            ✓                │
│                             │
│   You're all set.           │
│                             │
│   Slot 0   Uguisu       🔑 │
│   Slot 1   Jamie's iPhone ● │
│            OWNER            │
│   Slot 2   — empty —       │
│            GUEST            │
│   Slot 3   — empty —       │
│            GUEST            │
│                             │
│        [ Done ]             │
│                             │
└─────────────────────────────┘
```
*   Shows all 4 slots (0–3) with their current state and tier labels (`OWNER` / `GUEST`). The user's own slot is highlighted with a dot or accent indicator. This gives the user a first look at the full slot topology and access hierarchy.
*   Dismisses the onboarding modal and reveals the Home screen.

---

### 10.4 Home Screen (Key Fob)

The primary and only persistent screen. Stripped down to the essential interaction: the **3D Uguisu model** and the **gear icon**. All status indicators, labels, and toggles have been removed to keep the screen maximally clean.

```
┌─────────────────────────────┐
│ ⚙                          │
│                             │
│                             │
│                             │
│   ╔═══════════════════════╗  │
│   ║                       ║  │
│   ║   [Uguisu 3D model]   ║  │
│   ║   perspective, lit    ║  │
│   ║   LED: off (idle)      ║  │
│   ║                       ║  │
│   ║                       ║  │
│   ╚═══════════════════════╝  │
│     Tap · Hold to lock      │
│                             │
│                             │
│                             │
└─────────────────────────────┘
```

**Layout Elements:**

1.  **Gear Icon (top-left):** Opens Settings via the 3D flip transition (Section 10.4.2).
2.  **3D Uguisu Model (center, dominant):** A real-time photorealistic render of the Uguisu fob. This is the only interactive element on the screen.

    **Gesture model (mirroring the physical fob):**
    *   **Short press (tap):** Sends the **Unlock** AES-CCM payload.
    *   **Long press (~700 ms hold):** Sends the **Lock** AES-CCM payload automatically the instant the 700 ms threshold is reached — no release required. A haptic fires at the same moment the packet is sent.

    **Visual states (matching real Uguisu fire-and-forget behaviour — LED flashes once per action, never holds):**
    *   **Idle (locked or unlocked):** Model rendered under neutral ambient lighting. The Uguisu's RGB LED is unlit (dark). There is no persistent visual difference between locked and unlocked — the model looks the same at rest regardless of latch state.
    *   **Unlock flash (on tap):** Model physically depresses (a subtle 1–2 mm push-in animation on the tactile button geometry). LED flashes green once (fade in 100 ms, hold 200 ms, fade out 200 ms), then returns to dark. Model button springs back.
    *   **Lock flash (on 700 ms threshold):** Model depresses briefly. LED flashes red once (same fade in/hold/fade out timing as unlock). Then returns to dark. Model button springs back.
    *   **Proximity auto-action:** When the background service triggers an auto-unlock or auto-lock, the model briefly plays the corresponding button-press + LED flash animation (without user touch) to communicate what happened.

    **Passive interactivity:**
    *   The model responds to device gyroscope input with a subtle parallax tilt (±5°), giving a sense of physical depth. This is purely cosmetic and can be disabled in Settings for users who find it distracting.

3.  **Hint Label:** Below the model, a small secondary text label: *"Tap · Hold to lock"* (visible until the user performs their first intentional lock, then hidden permanently via `UserDefaults` / `SharedPreferences` flag).

---

### 10.4.1 Disconnect Overlay

When Guillemot is not reachable, the home screen shows a **full-screen semi-transparent overlay** instead of dimming the model. The overlay follows the system theme:
*   **Light mode:** White overlay, ~60% opacity.
*   **Dark mode:** Black overlay, ~60% opacity.

The Uguisu 3D model remains faintly visible beneath the overlay but is **non-interactive** (touch events are consumed by the overlay).

```
┌─────────────────────────────┐
│ ⚙                          │
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
│▒▒▒▒▒ ○ Disconnected ▒▒▒▒▒▒▒│
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
└─────────────────────────────┘
```

**States:**
*   `○ Disconnected` — Default when beacon is not detected. Shown as a centered label with a gray dot. No pulsing, no animation — the user can see at a glance that the vehicle is out of range.
*   `✕ Bluetooth is off` — Shown when BLE is disabled system-wide. Tapping the label deep-links to the OS Bluetooth settings.

The overlay **fades out** (200 ms) as soon as the Guillemot beacon is detected and a GATT connection is established. The gear icon remains accessible above the overlay so the user can always reach Settings regardless of connection state.

---

### 10.4.2 Settings Transition (3D Flip)

Tapping the gear icon triggers a **3D flip animation** that transitions from the Home screen to the Settings view:

1.  **Flip (0–400 ms):** The Uguisu 3D model begins a 180° rotation around its vertical axis (Y-axis), as if the user is flipping the physical fob over to look at its back. The model simultaneously scales up slightly (~1.1×) to fill more of the screen.
2.  **Morph (400–600 ms):** As the model completes the flip and shows its "back", the back surface cross-fades into the Settings content pane. The rounded edges of the model's silhouette expand to become the Settings sheet's rounded corners.
3.  **Settle (600–700 ms):** The Settings pane is fully visible and interactive.

**Reverse transition (closing Settings):** Tapping the close button (✕) or swiping down plays the animation in reverse — the Settings pane compresses back into the model's silhouette, the model flips 180° back to its front face, and settles into the Home screen position.

The transition is implemented using the platform's native 3D transform APIs:
*   **iOS:** `CATransform3D` with perspective (`m34`) on a snapshot layer, cross-fading to the Settings `UIViewController`.
*   **Android:** `View.rotationY` with `cameraDistance` on a shared element transition between the Compose surfaces.

---

### 10.4.3 3D Model Asset & Rendering

**Model source:**
The 3D model is derived from the Uguisu enclosure CAD once it is finalized. The PCB footprint is known (~35 × 46 mm, r ~2 mm rounded corners) and the enclosure envelope is ~50 × 35 × 12 mm. Until the enclosure CAD is complete, a **development placeholder** — a rounded-rectangle slab at the correct dimensions with a visible PCB texture — is used so rendering infrastructure can be built and validated independently of enclosure design.

The final model must include:
*   The enclosure body with accurate surface finish (3D-printed texture or smooth depending on final print).
*   The tactile button (SKQGABE010, 5.2 × 5.2 mm) as a distinct depressible geometry.
*   The RGB LED (XL-5050RGBC, SMD5050) as a separate emissive mesh controlled at runtime.
*   USB-C port cutout on the appropriate edge.

**Asset formats:**
*   `.glb` (glTF Binary) — used by Android (Filament/SceneView).
*   `.usdz` — used by iOS (RealityKit). Converted from the master `.glb` via Apple's `Reality Converter` or `usdzconvert`.
*   Both formats are bundled in the app (not downloaded at runtime). Total combined size target: < 5 MB.

**Rendering stack (fully native — no KMP sharing possible):**

| Platform | Library | Notes |
|---|---|---|
| iOS | **RealityKit** (`ARView` in non-AR mode) | PBR materials, environment lighting, entity animation. Gyroscope via `CMMotionManager`. |
| Android | **SceneView** (Filament-based, maintained Sceneform successor) | PBR materials, IBL environment map. Gyroscope via `SensorManager`. |

**Lighting:** Both platforms use an Image-Based Lighting (IBL) environment map simulating an outdoor environment (overcast sky). This gives the plastic/metal surfaces on the model a realistic appearance without requiring dynamic scene lights.

**LED emissive control at runtime:**
The LED mesh has its emissive color and intensity driven by the app state machine — not baked into the asset. The LED is **off by default** and only fires during transient flash animations (matching Uguisu's fire-and-forget behaviour):
*   `idle` (locked or unlocked) → emissive intensity: 0.0 (off)
*   `unlock flash` → emissive color: `#00FF00`, intensity ramps 0→1.0→0 over 500 ms (fade in 100 ms, hold 200 ms, fade out 200 ms)
*   `lock flash` → emissive color: `#FF0000`, same intensity ramp as unlock

The LED mesh is named `led_rgb` in the glTF scene graph so both platform renderers can address it by name at load time.

---

### 10.4.4 Haptics & Sound

Every interaction with the Uguisu model produces both a haptic and an optional sound response. Sound playback is conditional on the device's silent mode — sound never plays if the user has silenced their device.

**Haptic schedule:**

| Event | iOS (`UIImpactFeedbackGenerator`) | Android (`HapticFeedbackConstants` / `VibrationEffect`) |
|---|---|---|
| **Short press registered (unlock)** | `.light` impact | `KEYBOARD_TAP` |
| **Long press fires at 700 ms (lock payload sent)** | `.heavy` impact | `HapticFeedbackConstants.CONFIRM` (API 30+) or 80 ms `VibrationEffect.createOneShot` |
| **Disconnect overlay tap (rejected)** | `UINotificationFeedbackGenerator` `.error` | `HapticFeedbackConstants.REJECT` (API 30+) or double-short `VibrationEffect` |
| **Proximity auto-action (background service)** | `.light` (fires when app is foregrounded and model animates) | Same |
| **QR decryption animation resolve** | `.medium` impact | `LONG_PRESS` |

The long press is a single haptic event at the 700 ms threshold — the moment the packet fires. There is no mid-hold tick because the action is already complete; the heavy impact *is* the confirmation.

**Sound design:**

Both sounds are short, low-frequency mechanical clicks recorded or synthesised to evoke a physical tactile switch — not a UI chime. They should feel like the SKQGABE010 button on the actual Uguisu hardware.

*   **Unlock click:** A crisp, bright click (~40 ms, fast attack, no tail). Analogous to a single button press.
*   **Lock clunk:** A slightly heavier, deeper click (~60 ms, marginally slower attack). The extra weight communicates "this is a more deliberate action" without being alarming.

**Silent mode handling:**

*   **iOS:** Audio is played via `AVAudioPlayer` with the session category set to `.ambient`. This category automatically respects the hardware silent switch — no explicit mute check required.
*   **Android:** Before playback, check `AudioManager.getRingerMode()`. Play only if the result is `RINGER_MODE_NORMAL`. In `RINGER_MODE_VIBRATE` or `RINGER_MODE_SILENT`, skip audio entirely (haptics still fire since vibration is not affected by ringer mode on most devices).

Sound assets are bundled in the app as short `.caf` (iOS) and `.ogg` (Android) files, targeting < 20 KB each.

---

### 10.5 Settings & Key Management

Revealed via the 3D flip transition from the Home screen (Section 10.4.2). Key management is **inline** — slot rows are embedded directly in the Settings view, not behind a separate screen. Accessing the KEYS section requires an active BLE management connection (authenticated via SMP PIN). If not already connected, Pipit initiates the connection and the OS prompts for the 6-digit PIN when the user scrolls to or interacts with the KEYS section.

```
┌─────────────────────────────┐
│  Settings                 ✕ │
│─────────────────────────────│
│                             │
│  PROXIMITY                  │
│  ┌─────────────────────────┐│
│  │ Background Unlock  [ON] ││
│  │─────────────────────────││
│  │ Unlock Distance   ━━●━━ ││
│  │ (~2m / -65 dBm)        ││
│  │─────────────────────────││
│  │ Lock Distance     ━●━━━ ││
│  │ (~5m / -75 dBm)        ││
│  └─────────────────────────┘│
│                             │
│  KEYS                       │
│  ┌─────────────────────────┐│
│  │ Slot 0  Uguisu     🔑 ⋮││  ← ⋮ on Android only
│  │─────────────────────────││
│  │ Slot 1  Jamie's iPhone ⋮││
│  │─────────────────────────││
│  │ Slot 2       ⊕          ││
│  │─────────────────────────││
│  │ Slot 3       ⊕          ││
│  └─────────────────────────┘│
│                             │
│  DEVICE                     │
│  ┌─────────────────────────┐│
│  │ Transfer to New Phone ▶ ││
│  │─────────────────────────││
│  │ Change PIN (USB)      ▶ ││  ← Android only
│  │─────────────────────────││
│  │ Flash Firmware (USB)  ▶ ││  ← Android only
│  └─────────────────────────┘│
│                             │
│  ABOUT                      │
│  ┌─────────────────────────┐│
│  │ Version            1.0  ││
│  └─────────────────────────┘│
│                             │
└─────────────────────────────┘
```

Pipit detects the current phone's slot tier at launch (from the stored slot ID) and renders the appropriate settings variant. The two layouts share the same flip transition and visual shell — only the content differs.

---

### 10.5.1 Owner Settings (Slot 1)

```
┌─────────────────────────────┐
│  Settings                 ✕ │
│─────────────────────────────│
│                             │
│  PROXIMITY                  │
│  ┌─────────────────────────┐│
│  │ Background Unlock  [ON] ││
│  │─────────────────────────││
│  │ Unlock Distance   ━━●━━ ││
│  │ (~2m / -65 dBm)        ││
│  │─────────────────────────││
│  │ Lock Distance     ━●━━━ ││
│  │ (~5m / -75 dBm)        ││
│  └─────────────────────────┘│
│                             │
│  KEYS                       │
│  ┌─────────────────────────┐│
│  │ Slot 0  Uguisu     🔑 ⋮││
│  │─────────────────────────││
│  │ Slot 1  Owner's iPhone  ││  ← own slot, no ⋮
│  │         OWNER           ││
│  │─────────────────────────││
│  │ Slot 2  Guest 1        ⋮││
│  │         GUEST           ││
│  │─────────────────────────││
│  │ Slot 3       ⊕          ││
│  │         GUEST           ││
│  └─────────────────────────┘│
│                             │
│  DEVICE                     │
│  ┌─────────────────────────┐│
│  │ Transfer to New Phone ▶ ││
│  │─────────────────────────││
│  │ Change PIN (USB)      ▶ ││  ← Android only
│  │─────────────────────────││
│  │ Flash Firmware (USB)  ▶ ││  ← Android only
│  └─────────────────────────┘│
│                             │
│  ABOUT                      │
│  ┌─────────────────────────┐│
│  │ Version            1.0  ││
│  └─────────────────────────┘│
│                             │
└─────────────────────────────┘
```

**Sections:**

1.  **Proximity**
    *   **Background Unlock toggle:** Enables/disables the background BLE scanning service. On iOS, toggling ON checks location permission and prompts if not yet granted.
    *   **Unlock Distance slider:** Maps to the RSSI unlock threshold. Displayed in human-readable terms ("~2m") with the raw dBm value below. Range: -50 to -85 dBm. Default: -65 dBm.
    *   **Lock Distance slider:** Maps to the RSSI lock threshold. Range: -60 to -95 dBm. Default: -75 dBm.
    *   **Validation:** The lock threshold must always be at least 10 dBm below the unlock threshold (enforcing the hysteresis gap from Section 3.3). If the user drags one slider past the constraint, the other slider auto-adjusts.

2.  **Keys (Inline Key Management)**

    On first render, Pipit queries `SLOTS?` via the Management Command characteristic (triggering SMP pairing + `IDENTIFY` if not already connected) and populates the slot rows from the JSON response. Rows are always shown for all 4 slots. Each phone slot (1–3) displays a **tier label** below the device name — `OWNER` or `GUEST` in small caps, muted secondary color — so the owner can see the access hierarchy at a glance.

    *   **Slot 0 (Uguisu):** Shown with a key icon (🔑) and the name "Uguisu".
        *   **iOS:** Non-interactive. A subtle label *"Manage via Whimbrel"* indicates USB management is not available on iOS.
        *   **Android:** A **three-dot menu (⋮)** with:
            *   **Replace** — For swapping in a new physical Uguisu unit. This is an automated two-step flow:
                1. Prompts the user to connect the new fob via USB-C OTG, and provisions the generated Slot 0 key into the new fob via CDC serial (`PROV:0:<new_key>:0:Uguisu`).
                2. Pipit then automatically sends the same `PROV` command to Guillemot via the BLE Management Command characteristic to sync the vehicle with the new fob. The old fob is immediately locked out.
            *   No Rename or Delete options. The hardware fob's name is always "Uguisu" and Slot 0 cannot be vacated.

    *   **Slot 1 (owner's own slot):** Shows the slot number and device name. **No three-dot menu** — the owner cannot replace or delete their own slot from here. Use "Transfer to New Phone" in the DEVICE section to migrate.

    *   **Active guest slots (2–3):** Show the slot number and device name. A **three-dot menu button (⋮)** with:
        *   **Rename** — Opens a text input dialog (max 24 characters). On confirm, sends `RENAME:<slot>:<new_name>`.
        *   **Replace** — Launches the Replace Flow (Section 10.7). Revokes this slot and provisions a new phone in its place.
        *   **Delete** — Confirmation dialog: *"Revoke <name>? This device will be permanently locked out."* On confirm, sends `REVOKE:<slot>`. The row transitions to an empty-slot state.

    *   **Empty guest slots (2–3):** Show only a centered **⊕** icon. Tapping the row launches the Provision Guest Phone flow (Section 10.6).
    *   **Empty Slot 1:** Should not normally occur (Slot 1 is provisioned during initial Whimbrel setup). If it somehow appears empty, the ⊕ icon is not shown — Slot 1 can only be provisioned via Whimbrel.

3.  **Device**
    *   **Transfer to New Phone →** launches the Migration Flow (Section 10.8). This is the happy-path device upgrade where the owner has both phones in hand.
    *   **Change PIN (USB) →** *(Android only, hidden on iOS)* launches the Change PIN flow (Section 10.9). Requires USB-C OTG connection to Guillemot since `SETPIN` is serial-only (Section 6.1). On iOS, users must change the PIN via Whimbrel.
    *   **Flash Firmware (USB) →** *(Android only, hidden on iOS)* launches the USB Flashing flow (Section 10.10). Covers Guillemot `.uf2` flashing and Uguisu CDC serial provisioning.

4.  **About**
    *   App version, build number. Tap-and-hold for debug info (BLE state, stored slot ID, counter value).

---

### 10.5.2 Guest Settings (Slot 2–3)

A stripped-down variant with no key management, no USB operations, and no PIN access. The guest controls only their local proximity preferences and can transfer their own key.

```
┌─────────────────────────────┐
│  Settings                 ✕ │
│─────────────────────────────│
│                             │
│  PROXIMITY                  │
│  ┌─────────────────────────┐│
│  │ Background Unlock  [ON] ││
│  │─────────────────────────││
│  │ Unlock Distance   ━━●━━ ││
│  │ (~2m / -65 dBm)        ││
│  │─────────────────────────││
│  │ Lock Distance     ━●━━━ ││
│  │ (~5m / -75 dBm)        ││
│  └─────────────────────────┘│
│                             │
│  YOUR KEY                   │
│  ┌─────────────────────────┐│
│  │ Slot 2 · Guest 1       ││
│  │ Transfer to New Phone ▶ ││
│  └─────────────────────────┘│
│                             │
│  ABOUT                      │
│  ┌─────────────────────────┐│
│  │ Version            1.0  ││
│  └─────────────────────────┘│
│                             │
└─────────────────────────────┘
```

**Sections:**

1.  **Proximity** — Identical to owner. These are phone-local settings (stored in `UserDefaults` / `SharedPreferences`) and don't touch management. Guests have full control over their own proximity unlock behaviour.

2.  **Your Key** — A single-row section showing the guest's own slot ID and name. No three-dot menu, no slot list, no visibility into other slots.
    *   **Transfer to New Phone →** launches the Migration Flow (Section 10.8). The guest can migrate their own key to a new phone via QR. Since guest slots use plaintext QR (Section 7.1.1), the migration QR is also **unencrypted** — no PIN prompt during transfer.

3.  **About** — Same as owner. Tap-and-hold for debug info.

---

### 10.6 Provision Guest Phone Flow

Launched from Settings by tapping an **empty guest slot row (⊕)** (Slot 2 or 3). Since guest slots use unencrypted QR codes (Section 7.1.1), this flow requires **no PIN entry** from either party.

**Step 1 — Confirm**
```
┌─────────────────────────────┐
│                             │
│   Add a guest key?          │
│                             │
│   This will create a key    │
│   for Slot 2 (Guest 1).    │
│   The guest will be able    │
│   to lock and unlock only.  │
│                             │
│        [ Create Key ]       │
│        [ Cancel ]           │
│                             │
└─────────────────────────────┘
```
*   The target slot and default name are pre-determined: Slot 2 → "Guest 1", Slot 3 → "Guest 2".
*   No name input prompt. The owner can rename the slot later from the three-dot menu if desired.

**Step 2 — Provisioning & QR Generation**

Pipit executes the following automatically:
1.  Generates a random 16-byte AES key using the platform's secure random generator (`SecRandomCopyBytes` on iOS, `SecureRandom` on Android).
2.  Sends `PROV:<slot>:<key>:0:Guest 1` via the Management Command characteristic.
3.  Generates a **plaintext QR code**: `immogen://prov?slot=2&key=<hex>&ctr=0&name=Guest%201`

**Step 3 — QR Display**
```
┌─────────────────────────────┐
│                             │
│   Scan this on the          │
│   guest's phone             │
│                             │
│   ┌───────────────────┐     │
│   │                   │     │
│   │    [QR Code]      │     │
│   │                   │     │
│   └───────────────────┘     │
│                             │
│   The guest just needs to   │
│   scan — no PIN required.   │
│                             │
│        [ Done ]             │
│                             │
└─────────────────────────────┘
```
*   The QR contains the slot key in plaintext. No PIN is needed by the recipient.
*   **"Done"** dismisses the flow and returns to Settings. The slot row now shows "Guest 1" with an active indicator.

---

### 10.7 Replace Flow

Launched from the **three-dot menu → Replace** on an active slot that is *not* the user's own. This is the break-glass path for replacing a lost or compromised device. It combines revocation + provisioning + QR generation in a single sequence.

**Step 1 — Confirm Revocation**
```
┌─────────────────────────────┐
│                             │
│   ⚠ Replace "Jamie's       │
│   iPhone" (Slot 1)?         │
│                             │
│   This will permanently     │
│   lock out the old device   │
│   and create a new key      │
│   for a replacement phone.  │
│                             │
│   [ Replace ]               │
│   [ Cancel ]                │
│                             │
└─────────────────────────────┘
```

**Step 2 — Revoke, Provision & QR Generation**

Pipit executes automatically:
1.  `REVOKE:<slot>` — invalidates the old key.
2.  Generates a new random 16-byte AES key.
3.  `PROV:<slot>:<new_key>:0:<default_name>` — provisions the slot with the new key, counter reset to 0. Name is restored to the slot's default ("Guest 1" / "Guest 2" for guest slots; owner can edit after).
4.  Generates QR:
    *   **Guest slot (2–3):** Plaintext QR (no PIN, no Argon2id). `immogen://prov?slot=2&key=<hex>&ctr=0&name=Guest%201`
    *   **Owner slot (1):** PIN-encrypted QR (Argon2id, same as Section 7.1). Prompts the owner for their PIN before generating.

**Step 3 — QR Display**
*   Same layout as Provision Flow Step 3. For guest replacements, the display notes *"No PIN required."* For owner replacement, it notes the recipient will need the PIN.
*   **"Done"** returns to Settings. The slot row shows the default name.

**Self-Provisioning Recovery Variant:** When the Replace flow is entered from the **Onboarding camera screen → "Phone recovery >"**, the current phone has no key. The app displays an explicit blocker prompt before proceeding:
*   *"Please unlock your vehicle with your Uguisu key fob or a guest phone key to authorize recovery."*
Once the user confirms the vehicle is unlocked:
1.  Pipit connects to Guillemot and authenticates via SMP PIN (OS prompt).
2.  Pipit queries `SLOTS?` and displays the active phone slots for the user to choose which device to recover.
3.  Pipit automatically executes the `RECOVER:<slot>` command with a newly generated key (as per Section 7.3). 
4.  The new key is stored directly in the local secure keystore (no QR needed — the key is for this phone) and Pipit proceeds to the onboarding Done screen (Step 5).

---

### 10.8 Migration Flow (Transfer to New Phone)

Launched from Settings → "Transfer to New Phone". This is the happy-path device upgrade described in Section 7.2. The user has both phones in hand.

**Step 1 — Confirm Intent**
```
┌─────────────────────────────┐
│                             │
│   Transfer your key to a    │
│   new phone?                │
│                             │
│   This will generate a QR   │
│   code for your new phone   │
│   to scan. After the        │
│   transfer, this phone      │
│   will no longer work as    │
│   a key.                    │
│                             │
│   [ Generate QR Code ]      │
│   [ Cancel ]                │
│                             │
└─────────────────────────────┘
```

**Step 2 — PIN Entry (Owner Only)**
*   **Owner (Slot 1):** Standard 6-digit PIN input. The PIN derives the Argon2id encryption key for the QR payload. Proceeds to Step 3.
*   **Guest (Slot 2–3):** This step is **skipped entirely**. The migration QR for guest slots is plaintext (same as Section 7.1.1) — no PIN needed. Proceeds directly to Step 3.

**Step 3 — QR Display**
```
┌─────────────────────────────┐
│                             │
│   Scan this on your         │
│   new phone                 │
│                             │
│   ┌───────────────────┐     │
│   │                   │     │
│   │    [QR Code]      │     │
│   │                   │     │
│   └───────────────────┘     │
│                             │
│   Open Pipit on your new    │
│   phone and scan this code. │
│                             │
│   [ Done — I've Scanned ]   │
│   [ Cancel ]                │
│                             │
└─────────────────────────────┘
```
*   The QR encodes the `immogen://prov?` URI with the encrypted slot key, **current counter value**, slot ID, name, and Argon2id salt. The counter is included so the new phone picks up exactly where the old phone left off (no desync).

**Step 4 — Confirmation & Key Deletion**
*   Tapping **"Done — I've Scanned"** triggers a confirmation dialog: *"Are you sure? The key will be permanently deleted from this phone."*
*   On confirm: the slot key is deleted from the secure keystore. Pipit returns to the Onboarding camera screen (it no longer has a valid key).
*   **"Cancel"** at any point aborts without deleting anything.

---

### 10.9 Change PIN (Android Only)

Launched from Settings → "Change PIN (USB)". Requires a USB-C OTG connection to Guillemot because `SETPIN` is a serial-only command (Section 6.1). This flow is **hidden on iOS** — iOS users must change the PIN via Whimbrel.

**Step 1 — Connect Guillemot via USB**
```
┌─────────────────────────────┐
│                             │
│   Change Management PIN     │
│                             │
│   Connect Guillemot via     │
│   USB-C OTG to continue.    │
│                             │
│       ○ Waiting for USB...  │
│                             │
└─────────────────────────────┘
```
*   Pipit listens for a CDC serial device via `usb-serial-for-android`. On detection: *"Guillemot connected."* Proceeds to Step 2.

**Step 2 — Enter New PIN**
```
┌─────────────────────────────┐
│                             │
│   Enter new 6-digit PIN     │
│                             │
│     ┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐    │
│     │ ││ ││ ││ ││ ││ │    │
│     └─┘└─┘└─┘└─┘└─┘└─┘    │
│                             │
│   Confirm new PIN           │
│                             │
│     ┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐    │
│     │ ││ ││ ││ ││ ││ │    │
│     └─┘└─┘└─┘└─┘└─┘└─┘    │
│                             │
│        [ Change PIN ]       │
│        [ Cancel ]           │
│                             │
└─────────────────────────────┘
```
*   Two-field entry with confirmation. Both must match to proceed.

**Step 3 — Confirmation**
*   Pipit sends `SETPIN:<6digits>` via USB serial.
*   On success: *"PIN changed. All phones will need the new PIN to pair for management in future sessions."*
*   **Important note displayed:** *"Existing BLE bonds are not affected. Phones that are already paired will continue to work. The new PIN is only needed when a phone pairs for management access for the first time, or after clearing its Bluetooth pairing data."*

---

### 10.10 USB Flashing (Android Only)

Launched from Settings → "Flash Firmware (USB)". This section is **hidden entirely on iOS** since iOS does not support USB OTG host mode for UF2 mass storage or CDC serial.

```
┌─────────────────────────────┐
│  ◀ Flash Firmware           │
│─────────────────────────────│
│                             │
│   Connect your device       │
│   via USB-C OTG.            │
│                             │
│  ┌─────────────────────────┐│
│  │                         ││
│  │  [ Flash Guillemot ]    ││
│  │  .uf2 firmware update   ││
│  │                         ││
│  └─────────────────────────┘│
│  ┌─────────────────────────┐│
│  │                         ││
│  │  [ Flash Uguisu ]       ││
│  │  firmware + key flash   ││
│  │                         ││
│  └─────────────────────────┘│
│                             │
└─────────────────────────────┘
```

**10.10.1 Flash Guillemot (UF2)**

Flashes the Guillemot immobilizer firmware via USB OTG using the UF2 mass storage protocol.

1.  The user connects Guillemot to the Android phone via USB-C OTG cable and boots Guillemot into UF2 bootloader mode (double-tap reset).
2.  Pipit detects the UF2 mass storage volume via `libaums` (USB mass storage library for Android) and displays: *"Guillemot bootloader detected."*
3.  Pipit provides a file picker to select the `.uf2` firmware file, or bundles the latest firmware in the app assets.
4.  **"Flash"** → Pipit copies the `.uf2` file to the root of the mass storage volume. Guillemot automatically reboots with the new firmware.
5.  Progress bar during file copy. On completion: *"Guillemot firmware updated."*

**10.10.2 Flash Uguisu (CDC Serial)**

Flashes the Uguisu hardware fob firmware and provisions its Slot 0 key via USB OTG CDC serial.

1.  The user connects the Uguisu fob (via its XIAO nRF52840 USB-C) to the Android phone via USB-C OTG cable.
2.  Pipit opens a CDC serial connection using `usb-serial-for-android` and detects the Uguisu bootloader or serial console.
3.  **Firmware flash:** Pipit sends the compiled firmware binary via the nRF52840's serial DFU protocol. Progress bar during transfer.
4.  **Key provisioning:** After firmware flash (or if firmware is already current), Pipit can send serial commands to configure Uguisu's Slot 0 AES key via the `PROV:0:<key>:0:Uguisu` command format. *(Note: To complete the pairing, Pipit must simultaneously send this same `PROV` command to Guillemot over BLE, as described in Section 10.5.1).*
5.  On completion: *"Uguisu firmware updated and key provisioned."*

---

### 10.11 State Diagram — Home Screen

The Home screen 3D model and disconnect overlay reflect a combined state machine:

```
                    ┌──────────────────┐
          ┌────────▶│  Disconnected    │◀─── No beacon detected
          │         │  Overlay: ON     │
          │         │  Model: frozen   │
          │         └──────┬───────────┘
          │                │ Beacon detected + GATT connected
          │                ▼
          │         ┌──────────────────┐
          │    ┌───▶│  Connected       │
          │    │    │  + Locked        │
          │    │    │  Overlay: OFF    │
          │    │    └──────┬───────────┘
          │    │           │ User tap OR RSSI ≥ unlock threshold
          │    │           ▼
          │    │    ┌──────────────────┐
          │    │    │  Connected       │
          │    │    │  + Unlocked      │
          │    │    │  LED: off (idle) │
          │    │    └──────┬───────────┘
          │    │           │ User long press OR RSSI ≤ lock threshold
          │    │           │   OR beacon lost
          │    └───────────┘
          │                │ Beacon lost entirely
          └────────────────┘
```

**Key behaviors:**
*   The 3D model's LED is always off at rest — there is no persistent visual for lock/unlock state. The lock/unlock state is tracked internally and inferred from the Guillemot beacon UUID (Locked UUID vs Unlocked UUID, Section 3.2), but the model only shows LED flashes during actions.
*   When proximity mode is ON, the model plays the corresponding flash animation when a background service action fires (e.g., if the background service auto-unlocked while the app is foregrounded, the model plays the green flash).
*   The disconnect overlay and model interaction are driven by the same BLE event stream — there is no separate "manual mode" vs "proximity mode" state machine. Proximity mode simply automates the same gesture based on RSSI.

---

### 10.12 Error & Edge Case Handling

| Scenario | Behavior |
|---|---|
| **BLE off** | Disconnect overlay shows `✕ Bluetooth is off`. Tapping deep-links to OS Bluetooth settings. |
| **Location permission denied (iOS)** | Proximity toggle in Settings shows a warning: *"Proximity unlock requires 'Always Allow' location. Tap to open Settings."* The app still works as a manual key fob. |
| **Guillemot not found** | Disconnect overlay shows `○ Disconnected`. No timeout — overlay stays until beacon is detected. |
| **Management connection rejected (wrong PIN)** | OS-level SMP failure. Pipit shows: *"Pairing failed. Check your PIN and try again."* After 10 failures, Guillemot enters hard lockout (Section 5.3). Pipit shows: *"Device locked. Connect via USB to reset."* |
| **All phone slots full (provision attempt)** | All ⊕ rows are replaced by active device rows. The user must **Delete** an existing slot before provisioning a new phone. No empty slot = no ⊕ icon shown. |
| **Replace own slot attempted (owner)** | The **Replace** and **Delete** options are hidden in the three-dot menu for the owner's own slot (Slot 1). The owner must use "Transfer to New Phone" instead. |
| **Guest attempts restricted action** | If a guest somehow reaches a restricted management command (e.g., via a race condition or protocol manipulation), Guillemot returns `{"status":"error","code":"FORBIDDEN","msg":"guest slot"}`. Pipit shows: *"This action requires owner access."* |
| **IDENTIFY fails (wrong key / corrupted keystore)** | Guillemot returns MIC failure. Pipit shows: *"Identity verification failed. Your key may be corrupted. Contact the device owner."* Management session remains unbound. |
| **QR scan fails repeatedly** | After 3 failed scans, show a help link: *"Having trouble? Make sure the QR code is well-lit and fully visible."* |
| **Argon2id KDF slow on old hardware** | QR decryption animation pauses at the convergence phase and holds until KDF completes. No timeout — the animation simply waits. |
| **USB OTG device not detected (Android)** | Flash Firmware screen shows: *"No device detected. Make sure it's connected via USB-C OTG and in bootloader mode."* |
| **Change PIN — USB disconnected mid-write** | Pipit shows: *"Connection lost. PIN may not have been saved. Reconnect and try again."* The old PIN remains valid unless `SETPIN` succeeded before disconnect. |
| **Slot 0 Replace — Uguisu not connected via USB (Android)** | The Replace action prompts: *"Connect the new Uguisu fob via USB-C OTG."* Waits for USB detection before proceeding. |
| **Counter overflow** | The 4-byte counter supports ~4 billion operations. No UI handling needed for practical use. |