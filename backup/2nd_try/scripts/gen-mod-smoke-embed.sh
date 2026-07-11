#!/usr/bin/env bash
# Generate host/zephyr/generated/mod_smoke_wasm.c from build/mods/mod-smoke.wasm.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WASM="${ROOT}/build/mods/mod-smoke.wasm"
OUT="${ROOT}/host/zephyr/generated/mod_smoke_wasm.c"

"${ROOT}/scripts/build-mod-smoke.sh"

if [[ ! -f "${WASM}" ]]; then
	echo "gen-mod-smoke-embed: skipped (no mod-smoke.wasm)" >&2
	exit 0
fi

mkdir -p "$(dirname "${OUT}")"
python3 - "${WASM}" "${OUT}" <<'PY'
import sys

wasm_path, out_path = sys.argv[1], sys.argv[2]
data = open(wasm_path, "rb").read()

with open(out_path, "w", encoding="utf-8") as f:
    f.write("#include <stddef.h>\n\n")
    f.write("const unsigned char pm_mod_smoke_wasm[] = {\n")
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        line = ", ".join(f"0x{b:02x}" for b in chunk)
        f.write(f"  {line},\n")
    f.write("};\n")
    f.write(f"const unsigned int pm_mod_smoke_wasm_len = {len(data)}u;\n")
PY

echo "mod-smoke embed -> ${OUT}"
