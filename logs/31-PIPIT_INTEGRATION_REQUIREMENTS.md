# Pipit Integration: Technical Requirements

*Date: 2026-03-10*

**Status:** Technical Writeup
**Scope:** Complete specification of required changes to Guillemot firmware and Whimbrel dashboard to support the Pipit companion app.

---

## 1. Architectural Decisions

### 1.1 Multi-Device Key Slots (Critical)

Guillemot currently stores a single PSK (`g_psk`) and a single monotonic counter for replay protection. If Uguisu and Pipit shared these, unlocking via one would desync the other's counter — subsequent payloads from the lagging device would be rejected.

**Decision:** Guillemot supports **4 key slots** (0–3), each with independent PSK and counter. Slot 0 is reserved for the Uguisu fob; Slots 1–3 are for Pipit instances. All slots full = revoke before provisioning.

### 1.2 GATT Protocol Requirement

Uguisu broadcasts connectionlessly; Guillemot scans for it. Pipit cannot replicate this:

* **iOS background advertising is broken** — CoreBluetooth strips custom MSD and hashes Service UUIDs into an Apple-proprietary format. Guillemot cannot decode it.
* **BLE scanners cannot send data connectionlessly** — even with roles flipped (Guillemot advertising, Pipit scanning), there is no mechanism for a scanner to transmit a payload without connecting.

**Decision:** Guillemot hosts a GATT server. Pipit connects and writes an encrypted AES-CCM payload (identical format to Uguisu's MSD), routed through the existing `verify_payload()` logic.

---

## 2. BLE Architecture

### 2.1 Flipped Roles: Guillemot as Advertiser

To achieve reliable background wake-ups on both iOS and Android, the BLE roles are flipped from the Uguisu model:

1. **Guillemot (Central + Peripheral):**
   * Continues 5% duty-cycle scanning (25ms/500ms) for Uguisu fobs.
   * Implements **Stateful Beaconing**: Broadcasts a 500ms-interval beacon containing the "Immogen Proximity - Locked" Service UUID when locked, and switches to the "Immogen Proximity - Unlocked" Service UUID when unlocked.
2. **Pipit (Central/Scanner):**
   * Registers background scanners filtering for these specific UUIDs based on the desired target state.
   * **To Unlock (Approach):** Scans for the "Locked" UUID. On detection, evaluates the RSSI against the user's proximity threshold. If the signal meets the threshold, wakes in background, connects via GATT, and writes the Unlock payload.
   * **To Lock (Walk-Away):** Scans/monitors the "Unlocked" UUID. Evaluates RSSI against the user's walk-away threshold. When RSSI falls below the threshold or the beacon is lost, wakes in background, connects via GATT, and writes the Lock payload.

### 2.2 GATT Characteristic Structure

The "Immogen Proximity" service exposes three characteristics:

* **Unlock/Lock Command:** Receives a 19-byte encrypted AES-CCM payload (identical structure to Uguisu's MSD) as a "Write Without Response" (fire-and-forget). Routed through `verify_payload()`.
* **Management Command:** Requires an **Authenticated Link** (MITM protection) via standard BLE Pairing PIN. Used to receive management commands (slot queries, revocation, provisioning, and PIN changes).
* **Management Response:** `Notify` characteristic to asynchronously return responses to management commands. Output for data queries (e.g., `SLOTS?`) is formatted as JSON.

### 2.3 Slot Identification via Prefix Byte

The 19-byte command payload explicitly encodes its target Key Slot in the first byte to prevent Guillemot from brute-forcing AES decryption across all slots.
* **1-Byte Prefix Structure:** `(Slot ID << 4) | (Command)`
* **Upper 4 bits:** Target Key Slot (0-3). Slot 0 is strictly reserved for Uguisu.
* **Lower 4 bits:** Command ID (1 = Unlock, 2 = Lock).

### 2.4 Power Budget

| Metric | Value |
|---|---|
| Existing scan draw (5% duty) | ~306 µA |
| Beacon draw (500ms interval, ~2ms TX @ 5.3 mA) | ~21.2 µA |
| **Total standby** | **~327 µA** |
| Standby life (15.3 Ah scooter battery) | ~5.3 years (exceeds Li-ion self-discharge) |

The ~21 µA beacon overhead is negligible relative to the existing scan budget.

---

## 3. Provisioning Lifecycle

### 3.1 Initial Setup (Whimbrel, USB-C Serial)

The Whimbrel onboarding wizard:

1. **Flash Guillemot firmware** via USB-C DFU. *(Skippable for pre-flashed devices.)*
2. **Provision Uguisu (Slot 0):** Generate key → flash to fob via serial → prompt disconnect.
3. **Provision Guillemot (Slot 0):** Flash same key via `PROV:0:<key>:<counter>:Uguisu Fob`. No management PIN set — fob-only users manage slots exclusively via wired connection.
4. **"Add a phone key?"** — if declined, onboarding complete.
5. **Set management PIN:** User enters 6-digit PIN. Whimbrel sends `SETPIN:<hash>` and `PROV:1:<key>:<counter>:iPhone` via serial.
6. **Display QR code** (`immogen://prov?slot=1&key=<hex>&ctr=0&pin=<6digits>`) with obscure/reveal controls for the phone to scan.

### 3.2 Post-Installation Key Management (BLE)

Once a management PIN is set, the following operations are available over BLE from **Pipit** (native) or **Whimbrel** (Web Bluetooth), authenticated by the PIN:

* **Query slot status:** Read all 4 slots — occupied/empty, last-seen counter values.
* **Revoke a slot:** Zero PSK and reset counter, freeing it for re-provisioning.
* **Provision new phone (Slots 1–3):** Generate key → write to empty slot on Guillemot → display QR for target phone. Both Whimbrel and Pipit can serve as provisioning client.
* **Provision/replace Uguisu fob (Slot 0):** Generate key → write to Slot 0 over BLE → flash to fob via USB-C. **Android Pipit only** (USB OTG via `usb-serial-for-android`); iOS requires Whimbrel on a laptop.
* **Change management PIN:** Requires current PIN.

### 3.3 QR Code & Provisioning URI

URI format: `immogen://prov?slot=<n>&key=<hex>&ctr=0&pin=<6digits>`, parseable by the KMP shared module. Both Whimbrel (via `qrcode.js` or equivalent) and Pipit can generate and display QR codes.

---

## 4. Management PIN & Security

### 4.1 PIN Lifecycle (For Stateless Whimbrel Access)

A **6-digit management PIN** enables BLE key management post-installation, avoiding vehicle teardown for routine operations. 

**Why a PIN?** It allows the Whimbrel web dashboard to manage keys over BLE without requiring Whimbrel to store or maintain permanent memory of the vehicle's cryptographically secure 16-byte slot keys. A user can connect via any browser supporting Web Bluetooth, enter the PIN, and perform operations (like revoking a lost phone) statelessly.

**Security via BLE Pairing PIN:** The PIN is not sent as a plaintext payload or custom challenge. It acts as the **standard BLE Pairing PIN (SMP)**. When a client attempts to read/write the Management characteristics, the OS automatically prompts for the 6-digit PIN. This establishes a standard, fully encrypted and MITM-protected BLE connection, ensuring that sensitive management payloads (like new provisioning root keys) are never exposed to passive BLE sniffers.

The PIN is only set when the first phone is provisioned — until then, BLE management characteristics are not exposed. Stored as a hash in non-volatile flash.

### 4.2 Brute-Force Rate Limiting

* **Exponential backoff:** After 3 consecutive failures, lockout doubles starting at 5s (5s → 10s → 20s → 40s → ...).
* **Hard lockout:** After 10 consecutive failures, BLE management disabled entirely. Recovery requires USB-C serial via Whimbrel (`RESETLOCK`).
* Successful PIN entry resets the failure counter.

### 4.3 Recovery Fallback

USB-C serial via Whimbrel is the break-glass path for: all keys lost, PIN forgotten, or hard lockout triggered. Requires physical access to Guillemot's USB-C port (vehicle disassembly) — acceptable for a recovery-only path.

---

## 5. Required Changes: Guillemot Firmware

1. **Dual BLE roles:** `Bluefruit.begin(0, 1)` → `Bluefruit.begin(1, 1)` — Central (Uguisu scanning) + Peripheral (Pipit beacon) simultaneously.
2. **Stateful Proximity beacon:** 500ms advertising interval (`Bluefruit.Advertising.setInterval(800, 800)`). Dynamically updates the advertised Service UUID based on the latch state ("Immogen Proximity - Locked" vs "Immogen Proximity - Unlocked") to enable Pipit's walk-away auto-locking without continuous background wake-ups while riding.
3. **GATT server:** Implement the three-characteristic service described in §2.2, with write callbacks to handle Pipit connections and incoming payloads. Configure SoftDevice to enforce **Authenticated Link** security on the Management characteristics.
4. **Multi-slot storage:** Refactor `immo::CounterStore` and `immo_provisioning` for 4 independent key slots.
5. **PIN storage & rate limiting:** Hash in non-volatile flash; exponential backoff + hard lockout per §4.2.

### 5.1 Serial Protocol

| Command | Description |
|---|---|
| `PROV:<slot>:<key>:<counter>:[name]` | Provision a key into the specified slot. `name` is an optional human-readable device name string (e.g. `PROV:1:A1B2...:0:iPhone`). |
| `RENAME:<slot>:<name>` | Update the human-readable string associated with a slot without modifying the AES key or counter. |
| `SETPIN:<hash>` | Set or update the management PIN hash. |
| `SLOTS?` | Query status of all 4 key slots. Returns data in JSON format including slot ID, used status, last-seen counter, and device name. |
| `REVOKE:<slot>` | Zero a slot's PSK, reset its counter, and clear its name string. |
| `RESETLOCK` | Clear brute-force lockout counter (recovery). |

---

## 6. Required Changes: Whimbrel Dashboard

Whimbrel operates in two modes: **USB-C serial** (initial setup + recovery) and **Web Bluetooth** (post-installation key management).

* **Initial setup wizard:** Implements the onboarding flow from §3.1. The UI must handle the step sequence, QR display with obscure/reveal, and the "Skip" option for pre-flashed devices.
* **Post-installation management:** Implements the BLE operations from §3.2 via Web Bluetooth. The UI must present per-slot status, "Revoke" controls with confirmation, and QR display for new phone provisioning.
* **Serial protocol:** Implements the commands from §5.1 over both USB-C serial and Web Bluetooth transports.

---

## 7. Platform Capability Matrix

Capabilities differ due to iOS's lack of USB device APIs. BLE DFU has been evaluated and rejected — all firmware flashing uses USB-C (UF2 mass storage for Guillemot, CDC serial for Uguisu).

| Operation | Pipit (Android) | Pipit (iOS) | Whimbrel (laptop) |
|---|---|---|---|
| **Guillemot firmware flash** | USB OTG — `.uf2` via `libaums` | Not supported | USB-C DFU |
| **Uguisu firmware/key flash** | USB OTG — CDC serial via `usb-serial-for-android` | Not supported | Web Serial |
| **Key management (BLE)** | BLE GATT + PIN | BLE GATT + PIN | Web Bluetooth + PIN |
| **Phone provisioning** | BLE + QR display | BLE + QR display | Web Bluetooth + QR display |
| **Proximity unlock** | BLE background scan (Foreground Service) | BLE background scan (CoreBluetooth) | N/A |
| **Active key fob** | BLE GATT write | BLE GATT write | N/A |

This matrix determines what Whimbrel must continue to handle (firmware flashing, iOS fob provisioning) versus what can be delegated to Pipit.
