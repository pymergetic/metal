#!/usr/bin/env bash
# Linux wasm mod smoke — engine publishes bootstrap, wasmtime runs mod-smoke.wasm.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MOD="${ROOT}/build/mods/mod-smoke.wasm"
TOOLS="${ROOT}/.tools/wasmtime/bin/wasmtime"
VFS="${ROOT}/build/linux/vfs-mod"

"${ROOT}/scripts/build-mod-smoke.sh"

if [[ ! -f "${MOD}" ]]; then
	echo "linux mod verify: skipped (no mod-smoke.wasm)"
	exit 0
fi

if [[ ! -x "${TOOLS}" ]]; then
	"${ROOT}/scripts/setup-tools.sh"
fi

LINUX_ENGINE="${ROOT}/build/linux/engine/pm-linux-engine"
if [[ ! -x "${LINUX_ENGINE}" ]]; then
	"${ROOT}/scripts/build-linux.sh"
fi

rm -rf "${VFS}"
mkdir -p "${VFS}"
"${LINUX_ENGINE}" "${VFS}" >/dev/null

OUT="$(mktemp)"
cleanup() { rm -f "${OUT}"; }
trap cleanup EXIT

"${TOOLS}" run --dir "${VFS}"::/ "${MOD}" >"${OUT}" 2>&1

echo "=== linux mod-smoke ==="
cat "${OUT}"
echo

grep -q "mod-smoke: machine_ram=" "${OUT}"

echo "linux mod verify: ok"
