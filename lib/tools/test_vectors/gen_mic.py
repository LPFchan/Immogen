#!/usr/bin/env python3
"""Generate AES-128-CCM MIC test vectors for Uguisu/Guillemot BLE immobilizer."""

import argparse
import binascii
from cryptography.hazmat.primitives.ciphers.aead import AESCCM


def le16(x: int) -> bytes:
    return bytes([x & 0xFF, (x >> 8) & 0xFF])


def le32(x: int) -> bytes:
    return bytes(
        [
            x & 0xFF,
            (x >> 8) & 0xFF,
            (x >> 16) & 0xFF,
            (x >> 24) & 0xFF,
        ]
    )


def parse_int(s: str) -> int:
    return int(s, 0)


def parse_key(hex_str: str) -> bytes:
    b = binascii.unhexlify(hex_str.strip())
    if len(b) != 16:
        raise ValueError("key must be 16 bytes (32 hex chars)")
    return b


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Generate AES-128-CCM MIC for Uguisu/Guillemot test vectors."
    )
    ap.add_argument("--counter", type=parse_int, required=True)
    ap.add_argument("--command", type=parse_int, required=True)
    ap.add_argument(
        "--company-id",
        type=parse_int,
        default=0xFFFF,
        help="BLE MSD company ID, must match firmware (default 0xFFFF)",
    )
    ap.add_argument("--key", type=str, required=True, help="32 hex chars (16 bytes)")
    args = ap.parse_args()

    counter = args.counter & 0xFFFFFFFF
    command = args.command & 0xFF
    company_id = args.company_id & 0xFFFF

    nonce = le32(counter) + (b"\x00" * 9)  # 13 bytes
    msg = le32(counter) + bytes([command])  # 5 bytes

    aesccm = AESCCM(parse_key(args.key), tag_length=8)
    ct_and_tag = aesccm.encrypt(nonce, msg, None)
    tag = ct_and_tag[-8:]

    payload = msg + tag  # 13 bytes
    msd = le16(company_id) + payload

    print("nonce:", nonce.hex())
    print("msg:", msg.hex())
    print("mic:", tag.hex())
    print("payload_13B:", payload.hex())
    print("msd_company_plus_payload:", msd.hex())


if __name__ == "__main__":
    main()
