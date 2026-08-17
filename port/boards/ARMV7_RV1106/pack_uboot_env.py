"""Pack a U-Boot env text file into a CRC32-prefixed 256KiB NAND image.

Placeholders @IPADDR@ / @ETHADDR@ are filled from LUCKFOX_IP and
LUCKFOX_ETHADDR (default MAC 72:00:00:00:00:01, locally administered).
"""
import os
import pathlib
import struct
import sys
import zlib

ENV_SIZE = 0x40000
DEFAULT_MAC = "72:00:00:00:00:01"


def subst(text: str) -> str:
    if "@IPADDR@" not in text and "@ETHADDR@" not in text:
        return text
    ip = os.environ.get("LUCKFOX_IP", "")
    if not ip:
        raise SystemExit("pack_uboot_env: set LUCKFOX_IP to the board IPv4")
    mac = os.environ.get("LUCKFOX_ETHADDR", DEFAULT_MAC)
    return text.replace("@IPADDR@", ip).replace("@ETHADDR@", mac)


def pack(text: str) -> bytes:
    parts = []
    for line in subst(text).splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts.append(line.encode("ascii") + b"\0")
    body = b"".join(parts) + b"\0"
    data = body.ljust(ENV_SIZE - 4, b"\0")
    return struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF) + data


def main() -> None:
    src, dst = (pathlib.Path(p) for p in sys.argv[1:3])
    dst.write_bytes(pack(src.read_text(encoding="ascii")))


if __name__ == "__main__":
    main()
