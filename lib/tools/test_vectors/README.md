# Test vectors (Uguisu ↔ Guillemot MIC)

Shared helper to generate the expected 8-byte MIC (AES-CCM tag) for a `(counter, command)` tuple. Used by both Uguisu and Guillemot firmware development.

## Prereqs

- Python 3
- `cryptography` package: `pip install cryptography`

## Usage

```bash
python3 gen_mic.py --counter 1 --command 0x01 --company-id 0xFFFF --key 00112233445566778899aabbccddeeff
```

## Output

- `nonce`: 13-byte nonce (`counter_le(4) || 0x00*9`)
- `msg`: 5-byte message (`counter_le(4) || command(1)`)
- `mic`: 8-byte AES-CCM tag
- `payload_13B`: `counter(4) | command(1) | mic(8)`
- `msd_company_plus_payload`: `company_id(2) | payload_13B` (15 bytes total)

## Protocol

- Nonce: `counter_le(4) || 0x00×9` (13 bytes)
- Message: `counter_le(4) || command(1)` (5 bytes)
- Commands: `0x01` = Unlock, `0x02` = Lock
