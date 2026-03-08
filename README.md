# ImmoCommon — Ninebot G30 BLE Immobilizer Shared Library

ImmoCommon is the **shared library** module of a three-part immobilizer system ([Uguisu](https://github.com/LPFchan/Uguisu) fob + [Guillemot](https://github.com/LPFchan/Guillemot) receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. It provides the core cryptography, storage, and provisioning logic used by both the fob and the receiver.

This repository contains the **ImmoCommon C++ library**.

## Architecture & Responsibilities

Since both Uguisu and Guillemot run on the nRF52840 MCU and share the exact same BLE protocol and USB provisioning flow, ImmoCommon centralizes this logic to prevent duplication and ensure perfect synchronization.

ImmoCommon handles:

- **Cryptography (`immo_crypto`)**: Building nonces, message payloads, and generating AES-128-CCM Message Integrity Codes (MIC). Also provides constant-time equality checks to prevent timing attacks.
- **Provisioning (`immo_provisioning`)**: VBUS detection, USB serial reading, hex parsing, CRC-16-CCITT checksum validation, and the standard `PROV:` serial loop used by the Whimbrel web app.
- **Storage (`immo_storage`)**: `CounterStore` implementation using `Adafruit_LittleFS`. Handles atomic writes, CRC-32 integrity checks, and log rotation for the anti-replay counter. At 10 unlocks/day, standard NVS ~2.7 years; wear-leveling extends this.

## BLE Protocol

This section defines the encrypted BLE advertisement format and cryptography used between Uguisu and Guillemot. The protocol is advertisement-based with no persistent connection. Both devices share the same company ID, pre-shared key (PSK), and payload layout.

### Overview

- **Uguisu:** System OFF → button press → GPIO wake → broadcast encrypted advert ~2 s → System OFF.
- **Guillemot:** Duty-cycled scan, 20 ms / 2 s (1% duty, ~70 μA avg @ 3.3 V).
- **Key:** 128-bit pre-shared (injected via [Whimbrel](https://github.com/LPFchan/Whimbrel)). Guillemot accepts counter > last seen.

### Company ID

- **You choose:** any 16-bit value (e.g. `0xFFFF` for internal/prototype, or a Bluetooth SIG–assigned ID).
- Set `MSD_COMPANY_ID` in both firmwares: Guillemot [guillemot_config.example.h](https://github.com/LPFchan/Guillemot/blob/HEAD/firmware/guillemot/include/guillemot_config.example.h), Uguisu [uguisu_config.example.h](https://github.com/LPFchan/Uguisu/blob/HEAD/firmware/uguisu/include/uguisu_config.example.h) (or your local config). Both must use the same value.
- In adverts the value is sent little-endian (e.g. `0xFFFF` → `0xFF, 0xFF`).

### Pre-shared key (PSK)

- **128-bit (16 bytes)** AES key. Must be **identical** on Uguisu and Guillemot.
- **Provisioning:** Use [Whimbrel](https://github.com/LPFchan/Whimbrel) over Web Serial to write the same key to both devices. Never commit real keys.

### Advertising payload (13 bytes)

After the 2-byte company ID, the MSD payload is 13 bytes:


| Offset | Size | Field   | Endianness | Purpose                    |
| ------ | ---- | ------- | ---------- | -------------------------- |
| 0      | 4    | counter | little     | Anti-replay, monotonic     |
| 4      | 1    | command | —          | 0x01 = Unlock, 0x02 = Lock |
| 5      | 8    | mic     | —          | AES-128-CCM auth tag       |


Full MSD = `company_id_le(2) || counter_le(4) || command(1) || mic(8)` → 15 bytes total.

### AES-128-CCM MIC (8-byte tag)

- **Nonce (13 bytes):** `counter_le(4) || 0x00×9`.
- **Message (5 bytes):** `counter_le(4) || command(1)`.
- **Tag length:** 8 bytes (M=8, L=2 in CCM flags).
- MIC is computed over the 5-byte message with the 13-byte nonce and the 16-byte PSK; result is the 8-byte tag appended in the payload.

Receiver verifies: recomputes MIC over (counter, command) and compares to received MIC in constant time. Fob computes the same MIC when building the advert.

### Test vectors

From ImmoCommon root:

```bash
python3 tools/test_vectors/gen_mic.py --company-id <YOUR_ID> --counter 0 --command 1 --key <32 hex chars>
```

From Uguisu or Guillemot (ImmoCommon as submodule in `lib/ImmoCommon`):

```bash
python3 lib/ImmoCommon/tools/test_vectors/gen_mic.py --company-id <YOUR_ID> --counter 0 --command 1 --key <32 hex chars>
```

Output includes `msd_company_plus_payload` (15 bytes) you can inject or compare against firmware.

## Usage

ImmoCommon is included as a **Git Submodule** in both the `Uguisu` and `Guillemot` repositories.

### Test vectors

`tools/test_vectors/gen_mic.py` generates AES-128-CCM MIC test payloads for firmware development. See [tools/test_vectors/README.md](tools/test_vectors/README.md) for usage.

### Firmware inclusion

In PlatformIO projects, it is included in the `lib/` directory or referenced directly in `platformio.ini`.

```cpp
#include <ImmoCommon.h>

// Example: Using the counter store
immo::CounterStore store("/log.bin", "/old.bin", 4096);
store.begin();
uint32_t last = store.loadLast();

// Example: Provisioning loop
immo::ensure_provisioned(30000, on_success_callback, reload_callback, check_key_callback);
```

## Safety & Legal

- This is a prototype security/power-interrupt device. Use at your own risk.
- Not affiliated with Segway-Ninebot.
- **Do not test “lock” behavior while riding.**

