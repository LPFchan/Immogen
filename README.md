# ImmoCommon — Ninebot G30 BLE Immobilizer Shared Library

ImmoCommon is the **shared library** module of a three-part immobilizer system ([Uguisu](https://github.com/LPFchan/Uguisu) fob + [Guillemot](https://github.com/LPFchan/Guillemot) receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. It provides the core cryptography, storage, and provisioning logic used by both the fob and the receiver.

This repository contains the **ImmoCommon C++ library**.

## Architecture & Responsibilities

Since both Uguisu and Guillemot run on the nRF52840 MCU and share the exact same BLE protocol and USB provisioning flow, ImmoCommon centralizes this logic to prevent duplication and ensure perfect synchronization.

ImmoCommon handles:

- **Cryptography (`immo_crypto`)**: Building nonces, message payloads, and generating AES-128-CCM Message Integrity Codes (MIC). Also provides constant-time equality checks to prevent timing attacks.
- **Provisioning (`immo_provisioning`)**: VBUS detection, USB serial reading, hex parsing, CRC-16-CCITT checksum validation, and the standard `PROV:` serial loop used by the Whimbrel web app.
- **Storage (`immo_storage`)**: `CounterStore` implementation using `Adafruit_LittleFS`. Handles atomic writes, CRC-32 integrity checks, and log rotation for the anti-replay counter. At 10 unlocks/day, standard NVS ~2.7 years; wear-leveling extends this.

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