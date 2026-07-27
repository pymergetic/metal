#!/usr/bin/env bash
# Pack Miguel Grinberg's Microdot (all µPy extensions) into STORED-only
# mods/py/microdot.zip and sign with the Mods CA. Source is cloned into
# mods/py/microdot_src/ (optional; re-clone if missing).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${ROOT}/mods/py/microdot_src"
OUT_ZIP="${ROOT}/mods/py/microdot.zip"
URL="${PM_METAL_MICRODOT_URL:-https://github.com/miguelgrinberg/microdot.git}"

if [[ ! -d "${SRC_DIR}/src/microdot" ]]; then
	rm -rf "${SRC_DIR}"
	git clone --depth 1 "${URL}" "${SRC_DIR}"
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
