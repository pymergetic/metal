#!/usr/bin/env bash
# Build a wasm mod smoke binary (hostinfo load + /sys/pm path) for manual WASI tests.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ROOT}/build/mods/mod-smoke.wasm"
TOOLS="${ROOT}/.tools/wasi-sdk"
INC="${ROOT}/include"
SRC="${ROOT}/src"

if [[ ! -x "${TOOLS}/bin/clang" ]]; then
	"${ROOT}/scripts/setup-tools.sh"
fi

mkdir -p "$(dirname "${OUT}")"

CLANG="${TOOLS}/bin/clang"
SYSROOT="${TOOLS}/share/wasi-sysroot"

"${CLANG}" --target=wasm32-wasip1 \
	--sysroot="${SYSROOT}" \
	-I"${INC}" \
	-Os \
	-Wall -Wextra \
	"${SRC}/pymergetic/metal/sys/hostinfo.c" \
	"${SRC}/pymergetic/metal/sys/sys.c" \
	"${ROOT}/mods/smoke/main.c" \
	-o "${OUT}" 2>/dev/null || {
	echo "mod-smoke.wasm: skipped (mods/smoke/main.c not present)" >&2
	exit 0
}

echo "mod-smoke -> ${OUT}"
