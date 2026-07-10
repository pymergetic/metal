#!/usr/bin/env bash
# Twin slice: same orchestrator.wasm on linux + zephyr engine ports.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export WASMTIME="${WASMTIME:-${ROOT}/.tools/wasmtime/bin/wasmtime}"
WASM="${ROOT}/build/slice/orchestrator.wasm"
LINUX_ENGINE="${ROOT}/build/slice/engine/pm-linux-engine"
ZEPHYR_ENGINE="${ROOT}/build/slice/engine/pm-zephyr-engine"
VFS_LINUX="${ROOT}/build/slice/vfs-linux"
VFS_ZEPHYR="${ROOT}/build/slice/vfs-zephyr"
OUT_LINUX="$(mktemp)"
OUT_ZEPHYR="$(mktemp)"

cleanup() { rm -f "${OUT_LINUX}" "${OUT_ZEPHYR}"; }
trap cleanup EXIT

"${ROOT}/scripts/setup-tools.sh"
"${ROOT}/scripts/build-orchestrator.sh"
"${ROOT}/scripts/build-engines.sh"

run_engine() {
	local engine=$1 vfs=$2 out=$3
	rm -rf "${vfs}"
	mkdir -p "${vfs}"
	"${engine}" "${WASM}" "${vfs}" >"${out}" 2>&1
}

run_engine "${LINUX_ENGINE}" "${VFS_LINUX}" "${OUT_LINUX}"
run_engine "${ZEPHYR_ENGINE}" "${VFS_ZEPHYR}" "${OUT_ZEPHYR}"

echo "=== linux engine + orchestrator ==="
cat "${OUT_LINUX}"
echo
echo "=== zephyr engine + orchestrator ==="
cat "${OUT_ZEPHYR}"
echo

grep -q "pymergetic orchestrator" "${OUT_LINUX}"
grep -q "machine_ram = 536870912" "${OUT_LINUX}"
grep -q "ready" "${OUT_LINUX}"

grep -q "pymergetic orchestrator" "${OUT_ZEPHYR}"
grep -q "machine_ram = 134217728" "${OUT_ZEPHYR}"
grep -q "ready" "${OUT_ZEPHYR}"

echo "slice verify: ok"
