#!/usr/bin/env bash
# Pack utemplate (external/) + api package + precompiled templates into STORED zips.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HTTPD="${ROOT}/mods/httpd"
API="${ROOT}/mods/api"
UTEM="${ROOT}/external/utemplate"

if [[ ! -d "${UTEM}" ]]; then
	echo "pack_zips: missing ${UTEM} — run ./scripts/setup utemplate" >&2
	exit 1
fi

python3 "${API}/compile_templates.py"

python3 - "${ROOT}" <<'PY'
import sys, zipfile
from pathlib import Path

root = Path(sys.argv[1])
httpd = root / "mods/httpd"
api = root / "mods/api"
utem = root / "external/utemplate"


def pack_tree(src: Path, out: Path, arc_prefix: str) -> None:
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_STORED) as zf:
        for p in sorted(src.rglob("*.py")):
            if "__pycache__" in p.parts:
                continue
            arc = Path(arc_prefix) / p.relative_to(src)
            zf.write(p, arc.as_posix())
    print("pack_zips:", out, out.stat().st_size, "bytes")


pack_tree(utem, httpd / "utemplate.zip", "utemplate")
pack_tree(api / "api", api / "api.zip", "api")
pack_tree(api / "templates", api / "templates.zip", "templates")
PY
