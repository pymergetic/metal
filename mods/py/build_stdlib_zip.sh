#!/usr/bin/env bash
# Reproducible mods/py/stdlib.zip build: pack mods/py/stdlib_src/ (Easy
# pure-Python stdlib modules, see docs/MICROPYTHON.md) into a STORED-only
# (uncompressed) zip, then re-sign it with the Mods CA.
#
# STORED-only is not a style choice: py_zip_read.c's in-place archive
# reader (src/pymergetic/metal/py/py_zip_read.c) only understands
# compression method 0 — there is no DEFLATE decompressor in Metal. The
# packer below (plain Python zipfile, ZIP_STORED) enforces this at build
# time instead of failing silently at import time on-device, and avoids a
# hard dependency on the external `zip` CLI.
#
# Usage: ./mods/py/build_stdlib_zip.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${ROOT}/mods/py/stdlib_src"
OUT_ZIP="${ROOT}/mods/py/stdlib.zip"
OUT_SIG="${OUT_ZIP}.sig"

command -v python3 >/dev/null 2>&1 || {
	echo "build_stdlib_zip: python3 not found on PATH" >&2
	exit 1
}

[[ -d "${SRC_DIR}" ]] || {
	echo "build_stdlib_zip: missing ${SRC_DIR}" >&2
	exit 1
}

rm -f "${OUT_ZIP}"
python3 - "${SRC_DIR}" "${OUT_ZIP}" <<'PYEOF'
import os
import sys
import zipfile

src_dir, out_zip = sys.argv[1], sys.argv[2]
entries = []
for root, dirs, files in os.walk(src_dir):
    dirs.sort()
    for name in sorted(files):
        if name.endswith(".pyc"):
            continue
        full = os.path.join(root, name)
        rel = os.path.relpath(full, src_dir)
        entries.append(rel.replace(os.sep, "/"))

with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_STORED) as zf:
    for rel in entries:
        zf.write(os.path.join(src_dir, rel), rel)

print(f"build_stdlib_zip: packed {len(entries)} files")
PYEOF

echo "build_stdlib_zip: wrote ${OUT_ZIP}" >&2
python3 -c "
import zipfile
with zipfile.ZipFile('${OUT_ZIP}') as zf:
    for info in zf.infolist():
        print(f'  {info.compress_type:>2} {info.file_size:>7}  {info.filename}')
"

if [[ -x "${ROOT}/scripts/pki" ]]; then
	rm -f "${OUT_SIG}"
	"${ROOT}/scripts/pki" sign-wasm "${OUT_ZIP}"
	echo "build_stdlib_zip: signed -> ${OUT_SIG}" >&2
else
	echo "build_stdlib_zip: scripts/pki not found — leaving ${OUT_ZIP} unsigned" >&2
fi
