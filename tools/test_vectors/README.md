# Test vectors (Uguisu ↔ Guillemot MIC)

This folder contains a small helper to generate the expected 4-byte MIC (AES-CCM tag) for a `(device_id, counter, command)` tuple.

## Prereqs

- Python 3
- `cryptography` package

## Usage

```bash
python3 gen_mic.py --device-id 0x0001 --counter 1 --command 0x01 --key 00112233445566778899aabbccddeeff
```

The output includes:

- `payload_11B`: `device_id(2) | counter(4) | command(1) | mic(4)`
- `msd_company_plus_payload`: `company_id(2) | payload_11B`

