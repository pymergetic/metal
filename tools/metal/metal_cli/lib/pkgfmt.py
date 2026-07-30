"""Metal package payload format (inside wasm custom section metal.pkg)."""
from __future__ import annotations

import io
import struct
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path

from metal_cli.lib.lz4ustar import pack_dir_lz4_ustar, unpack_lz4_ustar

MAGIC = b"MTLP"
VERSION = 1
WASM_CUSTOM_SECTION_NAME = "metal.pkg"


@dataclass
class PackagePayload:
    manifest_toml: str
    files_dir: Path | None = None  # staged tree for pack
    lz4_blob: bytes | None = None
    ustar_uncompressed: int = 0


def encode_payload(manifest_toml: str, stage_dir: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="metal-pkg-") as td:
        blob_path = Path(td) / "tree.lz4"
        unc = pack_dir_lz4_ustar(stage_dir, blob_path)
        lz4 = blob_path.read_bytes()
    man = manifest_toml.encode("utf-8")
    buf = io.BytesIO()
    buf.write(MAGIC)
    buf.write(struct.pack("<I", VERSION))
    buf.write(struct.pack("<I", len(man)))
    buf.write(man)
    buf.write(struct.pack("<I", unc))
    buf.write(struct.pack("<I", len(lz4)))
    buf.write(lz4)
    return buf.getvalue()


def decode_payload(data: bytes) -> tuple[str, bytes, int]:
    if len(data) < 16 or data[:4] != MAGIC:
        raise ValueError("not a Metal package payload (bad magic)")
    ver = struct.unpack_from("<I", data, 4)[0]
    if ver != VERSION:
        raise ValueError(f"unsupported package version {ver}")
    off = 8
    mlen = struct.unpack_from("<I", data, off)[0]
    off += 4
    man = data[off : off + mlen].decode("utf-8")
    off += mlen
    unc = struct.unpack_from("<I", data, off)[0]
    off += 4
    llen = struct.unpack_from("<I", data, off)[0]
    off += 4
    lz4 = data[off : off + llen]
    if len(lz4) != llen:
        raise ValueError("truncated lz4 blob")
    return man, lz4, unc


def extract_payload_to(data: bytes, dest: Path) -> str:
    """Decode payload, write files under dest, return manifest toml."""
    man, lz4, unc = decode_payload(data)
    dest.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="metal-pkg-x-") as td:
        blob = Path(td) / "t.lz4"
        blob.write_bytes(lz4)
        unpack_lz4_ustar(blob, dest, uncompressed_len=unc)
    (dest / "MANIFEST.toml").write_text(man, encoding="utf-8")
    return man


def list_ustar_from_lz4(lz4: bytes, unc: int) -> list[str]:
    with tempfile.TemporaryDirectory(prefix="metal-pkg-ls-") as td:
        blob = Path(td) / "t.lz4"
        out = Path(td) / "out"
        blob.write_bytes(lz4)
        unpack_lz4_ustar(blob, out, uncompressed_len=unc)
        names = []
        for f in sorted(out.rglob("*")):
            if f.is_file():
                names.append(f.relative_to(out).as_posix())
        return names


# --- minimal wasm with one custom section ---


def _leb128(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def build_minimal_wasm_with_custom_section(section_name: str, payload: bytes) -> bytes:
    """Empty wasm module + custom section (name, payload)."""
    # Wasm header
    out = bytearray(b"\x00asm\x01\x00\x00\x00")
    # custom section id = 0
    name_b = section_name.encode("utf-8")
    body = _leb128(len(name_b)) + name_b + payload
    out.append(0)
    out.extend(_leb128(len(body)))
    out.extend(body)
    return bytes(out)


def extract_custom_section(wasm: bytes, section_name: str) -> bytes | None:
    if len(wasm) < 8 or wasm[:4] != b"\x00asm":
        return None
    off = 8
    want = section_name.encode("utf-8")
    while off < len(wasm):
        sid = wasm[off]
        off += 1
        size, off = _read_leb128(wasm, off)
        end = off + size
        if sid == 0:
            nlen, off2 = _read_leb128(wasm, off)
            name = wasm[off2 : off2 + nlen]
            payload = wasm[off2 + nlen : end]
            if name == want:
                return bytes(payload)
        off = end
    return None


def _read_leb128(buf: bytes, off: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while True:
        b = buf[off]
        off += 1
        result |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            break
        shift += 7
    return result, off
