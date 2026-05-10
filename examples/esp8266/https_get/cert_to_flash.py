#!/usr/bin/env python3
"""
cert_to_flash.py — Convert PEM certificates to ESP8266 axTLS flash images.

Usage:
  CA cert (server verification):
    python3 cert_to_flash.py ca  ca.pem  ca_flash.bin

  Client cert + private key (mutual TLS):
    python3 cert_to_flash.py client  client.pem  client_key.pem  client_flash.bin

Output format (CA):
  [uint16_t cert_len LE] [DER cert data] [0xFF pad to 4096 bytes]

Output format (client):
  [uint16_t cert_len LE] [DER cert] [uint16_t key_len LE] [DER key] [0xFF pad to 4096 bytes]

Flash these images with esptool.py:
  esptool.py write_flash 0x0EC000 ca_flash.bin       (CA cert)
  esptool.py write_flash 0x0ED000 client_flash.bin   (client cert + key)
"""

import sys
import struct

SECTOR_SIZE = 4096


def pem_to_der(pem_path: str) -> bytes:
    """Load a PEM file and return the raw DER bytes."""
    import base64
    with open(pem_path, "r") as f:
        lines = f.readlines()
    b64 = "".join(
        line.strip()
        for line in lines
        if not line.strip().startswith("-----")
    )
    return base64.b64decode(b64)


def make_ca_image(ca_pem: str) -> bytes:
    der = pem_to_der(ca_pem)
    if len(der) > SECTOR_SIZE - 2:
        raise ValueError(f"CA cert DER is {len(der)} bytes — exceeds sector capacity")
    blob = struct.pack("<H", len(der)) + der
    blob += b"\xFF" * (SECTOR_SIZE - len(blob))
    return blob


def make_client_image(cert_pem: str, key_pem: str) -> bytes:
    cert_der = pem_to_der(cert_pem)
    key_der  = pem_to_der(key_pem)
    header_bytes = 2 + 2  # two uint16_t length fields
    total = header_bytes + len(cert_der) + len(key_der)
    if total > SECTOR_SIZE:
        raise ValueError(
            f"cert ({len(cert_der)}B) + key ({len(key_der)}B) + headers "
            f"= {total}B — exceeds 4096-byte sector"
        )
    blob  = struct.pack("<H", len(cert_der)) + cert_der
    blob += struct.pack("<H", len(key_der))  + key_der
    blob += b"\xFF" * (SECTOR_SIZE - len(blob))
    return blob


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    mode = sys.argv[1].lower()

    if mode == "ca":
        if len(sys.argv) != 4:
            print("Usage: cert_to_flash.py ca  <ca.pem>  <output.bin>")
            sys.exit(1)
        data = make_ca_image(sys.argv[2])
        out  = sys.argv[3]

    elif mode == "client":
        if len(sys.argv) != 5:
            print("Usage: cert_to_flash.py client  <cert.pem>  <key.pem>  <output.bin>")
            sys.exit(1)
        data = make_client_image(sys.argv[2], sys.argv[3])
        out  = sys.argv[4]

    else:
        print(f"Unknown mode '{mode}'. Use 'ca' or 'client'.")
        sys.exit(1)

    with open(out, "wb") as f:
        f.write(data)

    print(f"Written {len(data)} bytes to {out}")
    if mode == "ca":
        der_len = struct.unpack_from("<H", data)[0]
        print(f"  CA cert DER: {der_len} bytes")
    else:
        cert_len = struct.unpack_from("<H", data, 0)[0]
        key_len  = struct.unpack_from("<H", data, 2 + cert_len)[0]
        print(f"  Client cert DER: {cert_len} bytes")
        print(f"  Private key DER: {key_len} bytes")


if __name__ == "__main__":
    main()
