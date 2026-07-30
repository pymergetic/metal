"""lz4(ustar) pack/unpack using Metal's vendored LZ4 (same as iface-pack)."""
from __future__ import annotations

import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

from metal_cli.paths import metal_root

_LZ4PACK: Path | None = None


def _ensure_lz4pack() -> Path:
    global _LZ4PACK
    if _LZ4PACK is not None and _LZ4PACK.is_file():
        return _LZ4PACK

    root = metal_root()
    lz4_c = root / "external" / "lz4" / "lib" / "lz4.c"
    lz4_h = root / "external" / "lz4" / "lib"
    if not lz4_c.is_file():
        raise FileNotFoundError(f"missing {lz4_c}")

    tmp = Path(tempfile.mkdtemp(prefix="metal-lz4pack-"))
    src = tmp / "lz4pack.c"
    src.write_text(
        """
#include <stdio.h>
#include <stdlib.h>
#include "lz4.h"
int main(void) {
  long n; unsigned char *in; int bound, out_n; unsigned char *out;
  if (fseek(stdin, 0, SEEK_END) != 0) return 1;
  n = ftell(stdin); if (n < 0 || fseek(stdin, 0, SEEK_SET) != 0) return 1;
  in = malloc((size_t)n); if (!in || fread(in, 1, (size_t)n, stdin) != (size_t)n) return 1;
  bound = LZ4_compressBound((int)n); out = malloc((size_t)bound);
  if (!out) return 1;
  out_n = LZ4_compress_default((const char *)in, (char *)out, (int)n, bound);
  if (out_n <= 0) return 1;
  if (fwrite(out, 1, (size_t)out_n, stdout) != (size_t)out_n) return 1;
  return 0;
}
""",
        encoding="utf-8",
    )
    out = tmp / "lz4pack"
    cc = os.environ.get("CC", "cc")
    subprocess.run(
        [cc, "-O2", f"-I{lz4_h}", "-o", str(out), str(src), str(lz4_c)],
        check=True,
    )
    _LZ4PACK = out
    return out


def pack_dir_lz4_ustar(stage: Path, out_blob: Path) -> int:
    """Pack stage dir as ustar (%P paths), lz4-compress to out_blob. Returns uncompressed size."""
    stage = stage.resolve()
    out_blob.parent.mkdir(parents=True, exist_ok=True)
    lz4pack = _ensure_lz4pack()
    with tempfile.TemporaryDirectory(prefix="metal-ustar-") as td:
        tar_path = Path(td) / "tree.tar"
        with tarfile.open(tar_path, "w", format=tarfile.USTAR_FORMAT) as tf:
            for f in sorted(stage.rglob("*")):
                if not f.is_file():
                    continue
                arc = f.relative_to(stage).as_posix()
                tf.add(f, arcname=arc)
        uncompressed = tar_path.stat().st_size
        with open(tar_path, "rb") as inf, open(out_blob, "wb") as outf:
            subprocess.run([str(lz4pack)], stdin=inf, stdout=outf, check=True)
        return uncompressed


def unpack_lz4_ustar(blob: Path, dest: Path, uncompressed_len: int | None = None) -> None:
    """Decompress lz4 ustar blob into dest. Prefer uncompressed_len from package header."""
    dest.mkdir(parents=True, exist_ok=True)
    root = metal_root()
    lz4_c = root / "external" / "lz4" / "lib" / "lz4.c"
    lz4_h = root / "external" / "lz4" / "lib"
    with tempfile.TemporaryDirectory(prefix="metal-unlz4-") as td:
        td_path = Path(td)
        dec_c = td_path / "lz4unpack.c"
        # argv[1] = expected uncompressed size (decimal), or 0 to grow
        dec_c.write_text(
            """
#include <stdio.h>
#include <stdlib.h>
#include "lz4.h"
int main(int argc, char **argv) {
  long n; unsigned char *in; int out_cap, out_n; unsigned char *out;
  out_cap = (argc > 1) ? atoi(argv[1]) : 0;
  if (fseek(stdin, 0, SEEK_END) != 0) return 1;
  n = ftell(stdin); if (n < 0 || fseek(stdin, 0, SEEK_SET) != 0) return 1;
  in = malloc((size_t)n); if (!in || fread(in, 1, (size_t)n, stdin) != (size_t)n) return 1;
  if (out_cap <= 0) out_cap = (int)n * 64;
  if (out_cap < (1 << 20)) out_cap = 1 << 20;
  for (;;) {
    out = malloc((size_t)out_cap);
    if (!out) return 1;
    out_n = LZ4_decompress_safe((const char *)in, (char *)out, (int)n, out_cap);
    if (out_n >= 0) break;
    free(out);
    out_cap *= 2;
    if (out_cap > (1 << 30)) return 1;
  }
  if (fwrite(out, 1, (size_t)out_n, stdout) != (size_t)out_n) return 1;
  return 0;
}
""",
            encoding="utf-8",
        )
        dec = td_path / "lz4unpack"
        cc = os.environ.get("CC", "cc")
        subprocess.run(
            [cc, "-O2", f"-I{lz4_h}", "-o", str(dec), str(dec_c), str(lz4_c)],
            check=True,
        )
        tar_path = td_path / "tree.tar"
        cmd = [str(dec)]
        if uncompressed_len is not None and uncompressed_len > 0:
            cmd.append(str(uncompressed_len))
        with open(blob, "rb") as inf, open(tar_path, "wb") as outf:
            subprocess.run(cmd, stdin=inf, stdout=outf, check=True)
        with tarfile.open(tar_path, "r:") as tf:
            tf.extractall(dest)
