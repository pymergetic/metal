#!/usr/bin/env bash
# Zephyr native_sim — build with WAMR and run mod-smoke.wasm via wasm_host.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NATIVE_MOD_BUILD="${ROOT}/build/zephyr/native_sim_mod"
NATIVE_TIMEOUT="${NATIVE_MOD_TIMEOUT:-45}"

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

if [[ ! -f "${ROOT}/mods/smoke/main.c" ]]; then
	echo "zephyr native_sim mod verify: skipped (no mods/smoke/main.c)"
	exit 0
fi

echo "=== build: native_sim/native/64 (mod) ==="
"${ROOT}/scripts/build-zephyr-native-sim-mod.sh"

OUT_NATIVE="$(mktemp)"
cleanup() { rm -f "${OUT_NATIVE}"; }
trap cleanup EXIT

echo
echo "=== run: native_sim/native/64 mod (${NATIVE_TIMEOUT}s) ==="
pm_zephyr_run_with_timeout "${NATIVE_TIMEOUT}" \
	"${NATIVE_MOD_BUILD}/zephyr/zephyr.exe" 2>&1 | tee "${OUT_NATIVE}" || true
echo

grep -q "mod-smoke: machine_ram=" "${OUT_NATIVE}"

echo "zephyr native_sim mod verify: ok"
