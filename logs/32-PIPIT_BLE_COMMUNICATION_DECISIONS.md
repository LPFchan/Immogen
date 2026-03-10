# Pipit & Whimbrel BLE Communication Architecture (Addendum)

*Date: 2026-03-10*

This document summarizes the final architectural decisions regarding the BLE communication protocols, security mechanisms, and payload structures between Guillemot, Pipit, and Whimbrel.

## 1. Security of Management Commands Over BLE
To allow Whimbrel (a stateless web dashboard) to manage Guillemot over BLE without permanently storing the vehicle's 16-byte slot keys, a **6-digit Management PIN** is used.

*   **Vulnerability Mitigated:** Transmitting the PIN or provisioning payloads (like new AES keys) in plaintext over an unencrypted GATT connection exposes them to passive sniffing.
*   **Resolution (Phase 1):** The system relies on **Standard BLE Pairing PIN (SMP)**. The 6-digit PIN acts as the static OS-level passkey. When Pipit or Whimbrel attempts to access Management characteristics, the host OS (iOS/Android/Windows) prompts the user for the PIN. This handles the mathematical handshake at the OS level, establishing a fully encrypted and MITM-protected BLE session *before* any sensitive data is transmitted.

## 2. Phase 2: WebAuthn Passkeys (Asymmetric Unlocking & Backup/Restore)
While the 6-digit BLE Pairing PIN handles Phase 1 securely, the ultimate Phase 2 goal is to replace both the 6-digit PIN and the 16-byte symmetric AES keys for the Pipit app entirely with **WebAuthn FIDO2 Passkeys**.

### The Problem with Symmetric Keys
Currently, the 16-byte AES-CCM root key is stored locally on the phone. If the user loses their phone or uninstalls the app, the key is gone. They must use Whimbrel (via a laptop) to provision a new slot.

### The Passkey Solution
By adopting FIDO2/CTAP2 over BLE:
1.  **Registration:** The phone OS (iOS/Android) generates an asymmetric P-256 Elliptic Curve keypair. The Private Key is securely synced to the user's cloud (iCloud Keychain / Google Password Manager). The Public Key is sent to Guillemot to store in a slot.
2.  **Unlocking:** Pipit asks the OS to silently sign a FIDO2 challenge from Guillemot. Guillemot verifies the signature against the stored Public Key to actuate the latch.
3.  **Automatic Backup & Restore:** If the user buys a new phone, their Passkey is automatically downloaded from the cloud. They simply open Pipit, and the OS can immediately sign unlock challenges without any re-provisioning process. 
4.  **No More PIN Prompts:** The 6-digit OS prompt is eliminated. The OS handles authorization via FaceID/TouchID natively.

### Implementation Feasibility & Resource Assessment
Implementing FIDO2/CTAP2 over BLE on a bare-metal nRF52840 is an advanced embedded security task. Here is a realistic breakdown of the required effort:

*   **Available Open-Source Resources:** The nRF52840 is highly supported in the FIDO2 ecosystem because its ARM TrustZone CryptoCell-310 provides hardware acceleration for ECDSA (secp256r1) signatures. There are robust open-source projects to reference, most notably **Google's OpenSK** (written in Rust) and various nRF Connect SDK Zephyr examples.
*   **The Porting Challenge (High Difficulty):** The difficulty lies not in writing the cryptography from scratch, but in decoupling the CTAP2 BLE transport layer and CBOR (Concise Binary Object Representation) parser from these monolithic security key projects. Most open-source libraries assume the entire chip is dedicated solely to being a security key. We must extract the FIDO2 logic and run it concurrently alongside Guillemot's primary immobilizer tasks, maintaining non-volatile storage for both FIDO credentials and legacy Uguisu AES keys.
*   **AI-Assisted Development:** Modern AI models are highly capable of generating CBOR parsers, structuring CTAP2 state machines, and integrating standard cryptographic libraries (like `mbedtls` or Nordic CryptoCell). However, because testing WebAuthn requires physical interaction between a mobile OS and the nRF52840 over BLE, an AI cannot test the handshake. The development cycle will require manual flashing and rigorous parsing of hex dumps from BLE packet sniffers to debug the FIDO2 state machine.
*   **Conclusion:** It is technically highly feasible but represents 2–4 weeks of specialized protocol debugging. It is definitively the superior architecture for user experience and cloud-native backup, strongly justifying its position as the ultimate Phase 2 objective.

## 3. Stateful Proximity Beaconing
To support both "approach-to-unlock" and "walk-away-to-lock" without causing severe battery drain on the phone while riding:

*   Guillemot advertises a continuous 500ms beacon.
*   **Stateful UUIDs:** The advertised Service UUID changes based on the latch state (`Immogen Proximity - Locked` vs `Immogen Proximity - Unlocked`).
*   **Approach:** When locked, Pipit scans for the "Locked" UUID. It evaluates the RSSI against a user-configurable threshold before connecting and sending the Unlock payload.
*   **Walk-Away:** Once unlocked, Pipit stops scanning for the "Locked" UUID and instead monitors the "Unlocked" UUID. When the RSSI drops below a walk-away threshold (or the beacon drops entirely), it connects and sends the Lock payload.

## 4. GATT Characteristic Structure
The `Immogen Proximity` GATT service consists of three characteristics:

1.  **Unlock/Lock Command (Write Without Response):** Receives the 19-byte encrypted AES-CCM payload. Like the Uguisu hardware fob, this is a "fire-and-forget" blind write. No custom GATT error feedback is required. Counter desyncs are handled gracefully by the standard RFC3610 counter window.
2.  **Management Command (Write, Authenticated Link):** Gated by the 6-digit BLE Pairing PIN. Receives administrative commands (e.g., `PROV`, `REVOKE`, `SLOTS?`).
3.  **Management Response (Notify):** Returns asynchronous responses to management commands. Output for data queries (like `SLOTS?`) is formatted as **JSON** for trivial parsing in KMP (Pipit) and Web Bluetooth (Whimbrel).

## 5. Payload Structure & Slot Identification
The 19-byte command payload structure is: `[1-byte Prefix] [4-byte Counter] [10-byte MAC] [4-byte Ciphertext]`.

To explicitly route payloads to the correct AES key and avoid brute-forcing decryption across slots, the 1-byte Prefix is bitwise packed:
*   **`Prefix = (Slot_ID << 4) | (Command_ID)`**
*   **Upper 4 bits:** Target Key Slot (0-3). Slot 0 is strictly reserved for the Uguisu connectionless broadcast. Pipit GATT payloads will specify Slots 1-3.
*   **Lower 4 bits:** Command ID (`1` = Unlock, `2` = Lock).

*Example:* `0x11` explicitly instructs Guillemot to pull the AES key for Slot 1 and execute Command 1 (Unlock).