# Architectural Decision Record: Removal of Provisioning Checksum

**Date:** 2026-03-12
**Status:** Accepted
**Context:** `PROV` command structure and serial parsing layer.

## Context
During the initial development of the Whimbrel/Immogen provisioning layer, the `PROV:` serial command utilized an application-level CRC-16 checksum to validate the 32-character hexadecimal key being provisioned over USB CDC serial:
`PROV:<key_32_hex>:<counter_8_hex>:<checksum_4_hex>`

With the introduction of the 4-slot Key Architecture and the shift to a BLE-first provisioning model (documented in `PIPIT_MASTER_ARCHITECTURE.md`), the payload was restructured to accommodate required metadata (Slot ID, Device Name) for remote management:
`PROV:<slot_id>:<key_32_hex>:<counter_8_hex>:<name_url_encoded>`

The CRC-16 checksum parameter was explicitly dropped from this new format.

## Rationale for Removal

1. **Protocol-Level Data Integrity is Redundant:**
   The new architecture routes provisioning commands through two primary transports:
   - **BLE GATT:** Authenticated Link Layer/L2CAP natively employs a 24-bit CRC on every packet. Data corruption over the air is virtually impossible to pass through the SoftDevice unflagged.
   - **USB CDC:** USB hardware layers already utilize CRC-16 framing for data packets.
   Therefore, an additional application-level checksum verification on a plaintext hex string provides no meaningful security or integrity benefit.

2. **BLE MTU Constraints:**
   The provisioning command is now sent verbatim over the BLE `Management Command` characteristic. Including the `slot_id` and an up to 24-character human-readable `name` alongside a 32-byte hex key significantly increases payload size. Dropping the unnecessary 4-byte checksum (+ separator) reclaims valuable bytes and helps comfortably fit within negotiated BLE MTU limits without forcing fragmentation.

3. **QR Code Encryption & Modernization:**
   The ecosystem shifted to using Argon2id for deriving an AES-128 key to encrypt the QR payload via AES-CCM. Since cryptographic integrity and authentication are handled entirely by AES-CCM (`MIC`) on the application (Pipit) side before the `PROV` command is ever formulated, checking the string's CRC before persisting it to `immo_storage` became obsolete.

## Implementation Details
- The C++ serial parsing layer (`immo_provisioning.cpp`) was updated on **2026-03-12** to parse the 4-parameter `PROV:` command and execute URL-decoding on the `name` string, removing the legacy `crc16_ccitt` computation.
- The web client (`Whimbrel` -> `crypto.js`) deprecated the `crc16Key` generator in commit `3bb6f9fa` (March 11, 2026).