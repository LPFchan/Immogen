# Test vectors (Uguisu ↔ Guillemot MIC)

Shared helper to generate the expected 4-byte MIC (AES-CCM tag) for a `(device_id, counter, command)` tuple. Used by both Uguisu and Guillemot firmware development.

## Prereqs

- Python 3
- `cryptography` package: `pip install cryptography`

## Usage

```bash
python3 gen_mic.py --device-id 0x0001 --counter 1 --command 0x01 --company-id 0xFFFF --key 00112233445566778899aabbccddeeff
```

## Output

- `nonce`: 13-byte nonce (`device_id_le(2) || counter_le(4) || 0x00*7`)
- `msg`: 7-byte message (`device_id_le(2) || counter_le(4) || command(1)`)
- `mic`: 4-byte AES-CCM tag
- `payload_11B`: `device_id(2) | counter(4) | command(1) | mic(4)`
- `msd_company_plus_payload`: `company_id(2) | payload_11B`

## Protocol

- Nonce: `device_id_le(2) || counter_le(4) || 0x00×7` (13 bytes)
- Message: `device_id_le(2) || counter_le(4) || command(1)` (7 bytes)
- Commands: `0x01` = Unlock, `0x02` = Lock
