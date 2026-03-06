# Guillemot / Uguisu BLE immobilizer protocol

Canonical spec for the encrypted fob → receiver advert format and crypto. The fob and Guillemot (receiver) must use the same company ID, PSK, and payload layout.

## Company ID

- **You choose:** any 16-bit value (e.g. `0xFFFF` for internal/prototype, or a Bluetooth SIG–assigned ID).
- Set it in Guillemot as `MSD_COMPANY_ID` in `guillemot_config.example.h` (or your config). When you add a fob, it must use the same company ID.
- In adverts the value is sent little-endian (e.g. `0xFFFF` → `0xFF, 0xFF`).

## Pre-shared key (PSK)

- **128-bit (16 bytes)** AES key. Must be **identical** on the fob and Guillemot.
- **Provisioning:** Use [Whimbrel](https://github.com/LPFchan/Whimbrel) over Web Serial to write the same key to both devices. Never commit real keys.

## Advertising payload (9 bytes)

After the 2-byte company ID, the MSD payload is 9 bytes:

| Offset | Size | Field    | Endianness | Description                    |
|--------|------|----------|------------|--------------------------------|
| 0      | 4    | counter  | little      | Monotonic counter (anti-replay). |
| 4      | 1    | command  | —           | 0x01 = Unlock, 0x02 = Lock.    |
| 5      | 4    | mic      | —           | AES-128-CCM tag (4 bytes).     |

Full MSD = `company_id_le(2) || counter_le(4) || command(1) || mic(4)` → 11 bytes total.

## AES-128-CCM MIC (4-byte tag)

- **Nonce (13 bytes):** `counter_le(4) || 0x00×9`.
- **Message (5 bytes):** `counter_le(4) || command(1)`.
- **Tag length:** 4 bytes (M=4, L=2 in CCM flags).
- MIC is computed over the 5-byte message with the 13-byte nonce and the 16-byte PSK; result is the 4-byte tag appended in the payload.

Receiver verifies: recomputes MIC over (counter, command) and compares to received MIC in constant time. Fob computes the same MIC when building the advert.

## Test vectors

Use ImmoCommon’s tools (shared submodule) with your PSK and company ID:

```bash
python3 firmware/guillemot/lib/ImmoCommon/tools/test_vectors/gen_mic.py --company-id <YOUR_ID> --counter 0 --command 1 --key <32 hex chars>
```

Output includes `msd_company_plus_payload` (11 bytes) you can inject or compare against firmware.
