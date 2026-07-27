#!/usr/bin/env bash
# Pack utemplate + precompiled templates into STORED zips for guest import.
# ESP import_stat only reports FILE for non-zip paths (no DIR), so loose
# package trees under /mods/py never import — zip is mandatory (same shape
# as microdot.zip / stdlib.zip).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${ROOT}/mods/py"

python3 "${PY}/compile_templates.py"

python3 - "${PY}" <<'PY'
import sys, zipfile
from pathlib import Path

py = Path(sys.argv[1])

def pack(src_name: str, out_name: str) -> None:
    src = py / src_name
    out = py / out_name
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_STORED) as zf:
        for p in sorted(src.rglob("*.py")):
            if "__pycache__" in p.parts:
                continue
            arc = Path(src_name) / p.relative_to(src)
            zf.write(p, arc.as_posix())
    print("pack_asgi_zips:", out, out.stat().st_size, "bytes")

pack("utemplate", "utemplate.zip")
pack("templates", "templates.zip")
PY
