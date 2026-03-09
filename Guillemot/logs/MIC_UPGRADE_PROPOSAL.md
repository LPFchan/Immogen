# Proposal: Cryptographic Upgrade to 8-Byte MIC

**To:** Project Management (Guillemot, Uguisu, ImmoCommon Repositories)
**From:** Engineering Team
**Date:** March 9, 2026

## 1. Executive Summary
This proposal outlines a coordinated effort to upgrade the cryptographic strength of the Guillemot/Uguisu immobilizer system by transitioning the AES-CCM Message Integrity Code (MIC) from its current 32-bit (4-byte) length to a modern 64-bit (8-byte) length. This upgrade directly addresses security critiques and enhances physical access control standards across the system.

## 2. Technical Feasibility & Payload Analysis
The current BLE advertisement payload for the immobilizer system consists of a total length of approximately 16 bytes. Under the legacy BLE 4.0 specification, an advertisement packet has a strict maximum payload capacity of **31 bytes**.

A payload size increase of 4 bytes (from a 4-byte MIC to an 8-byte MIC) will increase the total data payload to roughly **20 bytes**. This remains comfortably within the 31-byte limit, ensuring that we do not encounter fragmentation or require complex extended advertisement protocols. The transition is both mathematically and practically feasible.

## 3. Cross-Repository Impact & Required Changes
Implementing an 8-byte MIC requires simultaneous updates to the shared protocol logic and the specific handling in both the fob and the receiver.

*   **`ImmoCommon` (Shared Library):**
    *   Update cryptographic primitives in `immo_crypto.cpp`/`.h` to calculate and return an 8-byte MIC instead of the current 4-byte version.
    *   Refactor the `ccm_mic_4` function signature (or introduce a new `ccm_mic_8` implementation) and update the expected `CCM_MIC_LEN` constant from 4 to 8.

*   **`uguisu` (Fob Firmware):**
    *   Modify the BLE advertisement packet construction logic to broadcast the larger 13-byte active payload (4-byte monotonic counter + 1-byte command + 8-byte AES-CCM MIC).
    *   Adjust the buffer allocations within the advertising structure to prevent potential overflows when appending the larger MIC.

*   **`guillemot` (Receiver Firmware):**
    *   Update the payload parsing functions (`parse_payload_from_report`) to extract the 8-byte MIC accurately from the incoming manufacturer-specific data.
    *   Modify the constant-time expected size validation in `verify_payload()` to check an 8-byte length to maintain robust timing side-channel protection.

## 4. Rollout Strategy & Deployment
This upgrade introduces a **breaking protocol change**. A fob broadcasting an 8-byte MIC will not be recognized by a receiver expecting a 4-byte MIC, and vice versa.

To deploy this update without stranding users:

1.  **Simultaneous Firmware Release:** The firmware updates for both the `uguisu` fob and the `guillemot` receiver must be released and version-bumped concurrently.
2.  **App Coordination:** The `Whimbrel` provisioning and flashing web app must enforce a coordinated update. It should instruct the user to flash both devices in sequence before attempting to pair them.
3.  **Deprecation Plan:** The previous 4-byte protocol version should be fully deprecated upon the release of the 8-byte version, as maintaining dual-stack firmware significantly complicates validation logic and flash storage constraints.