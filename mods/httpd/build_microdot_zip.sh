#!/usr/bin/env bash
# Pack Microdot (external/microdot) into STORED mods/httpd/microdot.zip + Mods CA sig.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${ROOT}/external/microdot"
OUT_ZIP="${ROOT}/mods/httpd/microdot.zip"

if [[ ! -d "${SRC_DIR}/src/microdot" ]]; then
	echo "build_microdot_zip: missing ${SRC_DIR} — run ./scripts/setup microdot" >&2
	exit 1
fi

rm -f "${OUT_ZIP}"
python3 - "${SRC_DIR}/src" "${OUT_ZIP}" <<'PYEOF'
import os, sys, zipfile
src_dir, out_zip = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_STORED) as zf:
    root = os.path.join(src_dir, "microdot")
    for dirpath, _, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith(".py"):
                continue
            path = os.path.join(dirpath, name)
            arc = os.path.relpath(path, src_dir)
            zf.write(path, arc)
print("build_microdot_zip: wrote", out_zip)
PYEOF

"${ROOT}/scripts/pki" sign-wasm "${OUT_ZIP}"
echo "build_microdot_zip: signed -> ${OUT_ZIP}.sig"
