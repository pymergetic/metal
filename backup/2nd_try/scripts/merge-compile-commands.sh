#!/usr/bin/env bash
# Merge linux + zephyr compile_commands.json for clangd.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LINUX_DB="${LINUX_DB:-${ROOT}/build/linux/engine/compile_commands.json}"
ZEPHYR_DB="${ZEPHYR_DB:-${ROOT}/build/zephyr/ide/compile_commands.json}"
MERGED_DB="${ROOT}/build/ide/compile_commands.json"

if [[ ! -f "${LINUX_DB}" ]]; then
	echo "missing ${LINUX_DB} — run scripts/build-linux.sh" >&2
	exit 1
fi

if [[ ! -f "${ZEPHYR_DB}" ]]; then
	echo "missing ${ZEPHYR_DB} — run scripts/setup-ide.sh" >&2
	exit 1
fi

mkdir -p "$(dirname "${MERGED_DB}")"
python3 - "${LINUX_DB}" "${ZEPHYR_DB}" "${MERGED_DB}" <<'PY'
import json
import sys

paths = sys.argv[1:-1]
out = sys.argv[-1]
merged = []
seen = set()
for path in paths:
    with open(path) as f:
        for entry in json.load(f):
            key = entry.get("file")
            if key in seen:
                continue
            seen.add(key)
            merged.append(entry)
with open(out, "w") as f:
    json.dump(merged, f, indent=2)
print(f"merged {len(merged)} entries -> {out}")
PY

ln -sf "${MERGED_DB}" "${ROOT}/compile_commands.json"
echo "compile_commands.json -> ${MERGED_DB}"
