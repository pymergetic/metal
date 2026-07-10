#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ROOT}/build/slice/orchestrator.wasm"
TOOLS="${ROOT}/.tools/wasi-sdk"
INC="${ROOT}/include"

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
	"${ROOT}/guest/pymergetic/main.c" \
	"${ROOT}/guest/pymergetic/metal/sys/guestinfo.c" \
	"${ROOT}/guest/pymergetic/metal/sys/sys.c" \
	"${ROOT}/guest/pymergetic/metal/orchestrator/boot.c" \
	-o "${OUT}"

echo "orchestrator -> ${OUT}"
