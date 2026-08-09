#!/usr/bin/env bash
# Build-time only: emit metal product MPWP embeds into $OUT (never into src/).
# Does NOT forge pymergetic.wasmmod — that pack is owned by wasmmod embed-host.
set -euo pipefail
METAL="$(cd "$(dirname "$0")/.." && pwd)"
GEN="$METAL/scripts/gen_mpwp_pack.py"
OUT="${1:?usage: gen_product_packs.sh OUT_DIR}"
WWW="$METAL/src/pymergetic/metal/inspect/www/inspect"
INSPECT_PY="$METAL/src/pymergetic/metal/inspect"

mkdir -p "$OUT"

python3 - "$GEN" "$OUT" "$INSPECT_PY" "$WWW" <<'PY'
import shutil, subprocess, sys, tempfile
from pathlib import Path
gen, out, inspect_py, www = map(Path, sys.argv[1:5])
with tempfile.TemporaryDirectory() as td:
    root = Path(td)
    for name in ("__init__.py", "adapter_microdot.py", "app.py", "dispatch.py",
                 "self_desc.py", "stubs.py"):
        shutil.copy2(inspect_py / name, root / name)
    shutil.copytree(www, root / "www" / "inspect")
    (root / "meta").mkdir()
    (root / "meta" / "package.json").write_text(
        '{\n  "name": "pymergetic.metal.inspect",\n  "role": "app",\n'
        '  "product": "metal",\n  "org": "pymergetic"\n}\n',
        encoding="utf-8",
    )
    subprocess.check_call([
        sys.executable, str(gen),
        "--name", "pymergetic.metal.inspect",
        "--dir", str(root),
        "--out-mpwp", str(out / "pymergetic.metal.inspect.mpwp"),
        "--out-c", str(out / "pack_inspect_embed.c"),
        "--symbol", "pm_metal_pack_inspect",
    ])
PY

python3 "$GEN" \
  --name pymergetic.metal \
  --file "httpd.json=$METAL/httpd.json" \
  --out-mpwp "$OUT/pymergetic.metal.mpwp" \
  --out-c "$OUT/pack_metal_embed.c" \
  --symbol pm_metal_pack_metal

echo "OK: metal packs in $OUT (no wasmmod forgery)"
