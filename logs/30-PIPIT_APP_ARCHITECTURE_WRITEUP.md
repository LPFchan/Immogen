# Pipit: Companion App Architecture

*Date: 2026-03-10*

**Status:** Architectural Blueprint
**Scope:** Complete architecture for the Pipit companion iOS/Android app — tech stack, BLE design, key management, provisioning, and platform constraints.

---

## 1. Overview

**Pipit** is the companion app for the Immogen immobilizer ecosystem (following the ornithological naming: Guillemot, Uguisu, Whimbrel). It provides:

1. **Proximity Unlock (Background):** Low-power background BLE service that detects the vehicle and automatically unlocks Guillemot, with an adjustable proximity threshold.
2. **Active Key Fob (Foreground):** Manual lock/unlock UI, functionally identical to the Uguisu hardware fob.

Architecture: **Kotlin Multiplatform (KMP)** for shared business logic, with native UI (**UIKit** on iOS, **Jetpack Compose** on Android).

---

## 2. Tech Stack

### 2.1 Cross-Platform Core: KMP

Rather than duplicating business logic (Swift + Kotlin) or wrapping the C++ firmware codebase (`immo_crypto.cpp`) via JNI/Objective-C++, Pipit uses KMP.

* Native Kotlin on Android; compiles to a Swift-friendly native framework on iOS.
* Modern memory-safe environment vs. manual memory management through C++ wrappers.
* **Trade-off:** The C++ AES-CCM (RFC3610) crypto and MAC logic must be ported to pure Kotlin. The KMP implementation will be exhaustively unit-tested against the existing C++ test vectors for bit-for-bit parity.

### 2.2 UI: Platform-Native (Compose & UIKit)

No cross-platform UI frameworks (Flutter, React Native). BLE, camera (QR scanning), and secure enclave access are deeply intertwined with UI/UX — native toolkits ensure reliable platform API integration. UIKit is preferred over SwiftUI on iOS for mature BLE lifecycle management and finer control over background state transitions.

---

## 3. Key Slot Architecture

Guillemot supports **4 key slots** (0–3), each with an independent 16-byte PSK and monotonic counter for replay protection. Slot 0 is reserved for the Uguisu hardware fob; Slots 1–3 are for Pipit instances.

This solves the counter desync problem: if Uguisu and Pipit shared a single key/counter, unlocking via one would advance Guillemot's counter, causing the other device's payloads to be rejected until it "catches up." Independent slots eliminate this entirely.

When all slots are occupied, a new device cannot be provisioned until an existing slot is revoked.

---

## 4. BLE Architecture

### 4.1 The iOS Background Advertising Problem

The intuitive approach — phone as BLE Peripheral broadcasting its presence, Guillemot as Central scanning — fails on iOS. When backgrounded, iOS strips Manufacturer Data and hashes custom Service UUIDs into an undocumented, proprietary "overflow area." Guillemot (nRF52840) cannot decode this proprietary hash.

### 4.2 Solution: Guillemot as Advertiser, Pipit as Scanner

To achieve reliable background wake-ups on both platforms, the BLE roles are flipped:

1. **Guillemot (Central + Peripheral):**
   * Continues 5% duty-cycle scanning (25ms/500ms) for Uguisu fobs.
   * Simultaneously broadcasts a 500ms-interval beacon with a custom "Immogen Proximity" Service UUID.
2. **Pipit (Central/Scanner):**
   * Implements a **Stateful Beaconing** strategy to support both approach-to-unlock and walk-away-to-lock without continuous background wake-ups while riding.
   * **When Locked:** Scans for the "Immogen Proximity - Locked" UUID. On detection, it evaluates the beacon's RSSI against a user-configurable proximity threshold. If the signal is strong enough, it connects via GATT and sends the Unlock payload.
   * **When Unlocked:** Scans for the "Immogen Proximity - Unlocked" UUID. Monitors RSSI against the user's walk-away threshold. When RSSI drops below this threshold (or the beacon is lost entirely), it connects via GATT and sends the Lock payload.

**iOS caveats:** CoreBluetooth batches background scan delivery — latency ranges from sub-second to several minutes depending on iOS version, device state, and app recency. `CBCentralManagerScanOptionAllowDuplicatesKey` is ignored in background mode. Implementation must use `centralManager:willRestoreState:` for state restoration.

### 4.3 Why GATT (Not Connectionless)

Even with the roles flipped, Pipit cannot send its unlock payload connectionlessly — the BLE specification provides no mechanism for a scanner to transmit custom data (e.g., inside a Scan Request). Pipit must establish a GATT connection to write the payload.

### 4.4 GATT Characteristic Structure

Guillemot's GATT server exposes the "Immogen Proximity" service with three characteristics:

* **Unlock/Lock Command:** Pipit writes a 19-byte encrypted AES-CCM payload (identical structure to Uguisu's MSD). This is a "Write Without Response" (fire-and-forget), matching the blind-broadcast nature of the hardware fob.
* **Management Command:** Requires an **Authenticated Link** (MITM protection) via standard BLE Pairing PIN. Used for slot queries, revocation, provisioning, and PIN changes.
* **Management Response:** `Notify` characteristic to asynchronously return responses to management commands. Responses (like the output of `SLOTS?`) are formatted as JSON for trivial parsing in KMP and Web Bluetooth (e.g., `{"slots":[{"id":0,"used":true},{"id":1,"used":false}]}`).

### 4.5 Slot Identification via Prefix Byte

The 19-byte payload structure is: `[1-byte Prefix] [4-byte Counter] [10-byte MAC] [4-byte Ciphertext]`.
To avoid brute-forcing decryption across all 4 key slots, the 1-byte Prefix explicitly encodes both the Slot ID and the Command:
* **Upper 4 bits:** Slot ID (0-3)
* **Lower 4 bits:** Command (1 = Unlock, 2 = Lock)

Example: `0x11` explicitly tells Guillemot to pull the AES key for Slot 1 and verify an Unlock command. Slot 0 is strictly reserved for the Uguisu fob.

### 4.6 Unlock Latency (GATT Path)

Unlike Uguisu's connectionless broadcast (detected in a single scan window), Pipit's path involves sequential GATT operations post-wake:

| Step | Typical Latency |
|---|---|
| OS background scan delivery | Sub-second to minutes (variable) |
| GATT connection establishment | ~50–200 ms |
| Service & characteristic discovery | ~100–300 ms |
| Encrypted payload write + ACK | ~10–50 ms |
| `verify_payload()` + latch actuation | ~5–15 ms |
| **Total (post-wake)** | **~165–565 ms** |

The post-wake GATT path is near-instant. The dominant variable is OS scan delivery — proximity unlock via Pipit will not feel as instantaneous as a physical fob press, particularly on iOS.

---

## 5. Provisioning & Key Management

### 5.1 QR Code Provisioning

Pipit is provisioned by scanning a QR code displayed by **Whimbrel** (initial setup) or **another Pipit instance** (post-installation). The QR encodes:

```
immogen://prov?slot=<n>&key=<hex>&ctr=0&pin=<6digits>
```

* QR codes provide a high-bandwidth, air-gapped transfer of root cryptographic keys — no pairing over an unencrypted BLE channel.
* Slot keys and management PIN are stored strictly in hardware-backed keystores (**Android Keystore** / **iOS Keychain**) — never in plaintext storage.
* The KMP shared module parses the provisioning URI. Both Whimbrel and Pipit can generate and display QR codes.

### 5.2 Onboarding Flow (Whimbrel)

The initial provisioning is driven by Whimbrel over USB-C serial:

1. Flash Guillemot firmware via USB-C DFU. *(Skippable for pre-flashed devices.)*
2. Provision Uguisu (Slot 0): Generate key → flash to fob via serial → prompt disconnect.
3. Provision Guillemot (Slot 0): Flash same key via serial. No management PIN set yet.
4. "Add a phone key?" — if declined, onboarding complete.
5. Set management PIN: User enters 6-digit PIN. Whimbrel sends PIN hash and provisions Slot 1 via serial.
6. Display QR code for the phone to scan (with obscure/reveal controls).

### 5.3 BLE Key Management (Post-Installation)

Once a management PIN is set, Pipit can authenticate to Guillemot over BLE and perform:

* **Query slot status:** Read all 4 slots — occupied/empty, last-seen counter values.
* **Revoke a slot:** Zero PSK and reset counter, freeing it for re-provisioning.
* **Provision new phone (Slots 1–3):** Generate key → write to empty slot on Guillemot → display QR for target phone.
* **Provision/replace Uguisu fob (Slot 0):** Generate key → write to Slot 0 over BLE → flash to fob via USB-C. **Android only** (USB OTG via `usb-serial-for-android`); iOS requires Whimbrel on a laptop.
* **Change management PIN:** Requires current PIN.

### 5.4 Management PIN (For Stateless Whimbrel Access)

A **6-digit management PIN** enables BLE key management post-installation, avoiding vehicle teardown for routine operations. 

**Why a PIN?** It allows the Whimbrel web dashboard to manage keys over BLE without requiring Whimbrel to store or maintain permanent memory of the vehicle's cryptographically secure 16-byte slot keys. A user can connect via any browser supporting Web Bluetooth, enter the PIN, and perform emergency operations (like revoking a lost phone) completely statelessly.

**Security via BLE Pairing PIN:** Instead of a custom plaintext PIN challenge (which would expose new provisioning keys to passive BLE sniffers), the PIN acts as the **standard BLE Pairing PIN (SMP)**. When Whimbrel or Pipit attempts to read/write the Management characteristics, the OS automatically prompts for the 6-digit PIN, establishing a fully encrypted and MITM-protected BLE session before any commands are transmitted.

The PIN is only set when the first phone is provisioned — until then, Guillemot does not expose BLE management characteristics.

**Brute-force protection (Guillemot-enforced):**
* Exponential backoff after 3 consecutive failures: 5s → 10s → 20s → 40s → ...
* Hard lockout after 10 consecutive failures — BLE management disabled entirely. Recovery requires USB-C serial via Whimbrel.
* Successful PIN entry resets the failure counter.

Pipit must handle lockout feedback gracefully in its UI (display remaining wait time, indicate hard lockout state).

### 5.5 Recovery Fallback

USB-C serial via Whimbrel is the break-glass path for: all keys lost, PIN forgotten, or hard lockout triggered. Requires physical access to Guillemot's USB-C port (vehicle disassembly) — acceptable for a recovery-only path. Pipit should direct users to Whimbrel in these scenarios.

---

## 6. Platform Capability Matrix

Capabilities differ due to iOS's lack of USB device APIs. BLE DFU has been evaluated and rejected — all firmware flashing uses USB-C (UF2 mass storage for Guillemot, CDC serial for Uguisu).

| Operation | Pipit (Android) | Pipit (iOS) | Whimbrel (laptop) |
|---|---|---|---|
| **Guillemot firmware flash** | USB OTG — `.uf2` via `libaums` | Not supported | USB-C DFU |
| **Uguisu firmware/key flash** | USB OTG — CDC serial via `usb-serial-for-android` | Not supported | Web Serial |
| **Key management (BLE)** | BLE GATT + PIN | BLE GATT + PIN | Web Bluetooth + PIN |
| **Phone provisioning** | BLE + QR display | BLE + QR display | Web Bluetooth + QR display |
| **Proximity unlock** | BLE background scan (Foreground Service) | BLE background scan (CoreBluetooth) | N/A |
| **Active key fob** | BLE GATT write | BLE GATT write | N/A |

### 6.1 Android USB Details

**Guillemot DFU (UF2):** The Xiao nRF52840 Adafruit bootloader presents as USB mass storage in DFU mode (double-tap reset). Pipit uses `libaums` (pure Java, no root) to mount the FAT filesystem and copy the `.uf2`. Must handle USB re-enumeration (CDC serial → mass storage) via `ACTION_USB_DEVICE_ATTACHED/DETACHED` receivers.

**Uguisu serial:** `usb-serial-for-android` opens CDC/ACM connections. Auto-detection by interface type supported since v3.5.0 (no VID/PID probing).

**Gotchas:** Android requires explicit USB permission per device (`UsbManager.requestPermission()`). Phone must supply 5V via USB-C OTG — most 2020+ devices support this; low-end devices may not.

### 6.2 iOS Limitations (Confirmed)

No public API for generic USB device communication. Investigated and ruled out:
* `ExternalAccessory` — requires MFi certification.
* DriverKit/USBDriverKit — iPadOS M1+ only.
* WebUSB — unsupported in Safari.
* USB CDC serial — no API.
* UF2 via Files app — unreliable (iOS writes `.Trashes`/`.fseventsd` metadata that confuses the bootloader).

**Consequence:** iOS users needing firmware flashing or Uguisu fob provisioning must use Whimbrel on a laptop. All BLE operations work identically on both platforms.

---

## 7. Implementation Phases

1. **KMP Scaffolding:** Monorepo with `shared`, `androidApp`, `iosApp`.
2. **Crypto Porting:** Pure-Kotlin AES-128 and RFC3610 MIC generation in `shared`, validated against C++ test vectors.
3. **Platform Interfaces (`expect`/`actual`):** KMP wrappers for secure storage (Keychain/Keystore) and BLE Central managers.
4. **Native UI:** QR scanning provisioning flow, key fob interface, and slot management screens (Compose / UIKit).
5. **Background Services:** Android Foreground Service and iOS CoreBluetooth background modes for proximity wake-up and automated authentication.
