#!/usr/bin/env python3
"""Prefix a Rockchip 472 payload with the 512-byte SRAM header."""
from __future__ import annotations

import pathlib
import struct
import sys


def pack(raw: bytes) -> bytes:
    header = (
        struct.pack("<I", 0xEA00007E)
        + bytes(4)
        + b"RSAK"
        + struct.pack("<I", 0x1F8)
        + bytes(0x200 - 16)
    )
    return header + raw


def main() -> None:
    src, dst = (pathlib.Path(p) for p in sys.argv[1:3])
    dst.write_bytes(pack(src.read_bytes()))


if __name__ == "__main__":
    main()
