# Test vectors (receiver)

This folder generates test payloads that match the receiver firmware’s MIC verification scheme.

## Setup

```bash
python3 -m pip install cryptography
```

## Generate an unlock payload + MIC

```bash
python3 gen_mic.py --device-id 0x1234 --counter 1 --command 0x01 --company-id 0xFFFF --key 00000000000000000000000000000000
```

Notes:
- Nonce scheme in firmware: `nonce = device_id_le(2) || counter_le(4) || 0x00 * 7` (13 bytes)
- Message authenticated: `device_id_le(2) || counter_le(4) || command(1)`

