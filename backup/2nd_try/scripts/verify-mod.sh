#!/usr/bin/env bash
# Wasm mod smoke — linux (wasmtime) + zephyr native_sim + zephyr qemu (WAMR).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKIP_LINUX="${SKIP_MOD_LINUX:-0}"
SKIP_ZEPHYR_NATIVE_SIM="${SKIP_MOD_ZEPHYR_NATIVE_SIM:-0}"
SKIP_ZEPHYR_QEMU="${SKIP_MOD_ZEPHYR_QEMU:-0}"

if [[ "${SKIP_LINUX}" == "1" ]]; then
	echo "=== verify: mod linux (skipped — SKIP_MOD_LINUX=1) ==="
else
	echo "=== verify: mod linux ==="
	"${ROOT}/scripts/verify-mod-linux.sh"
fi
echo

if [[ "${SKIP_ZEPHYR_NATIVE_SIM}" == "1" ]]; then
	echo "=== verify: mod zephyr native_sim (skipped — SKIP_MOD_ZEPHYR_NATIVE_SIM=1) ==="
else
	echo "=== verify: mod zephyr native_sim ==="
	"${ROOT}/scripts/verify-mod-zephyr-native-sim.sh"
fi
echo

if [[ "${SKIP_ZEPHYR_QEMU}" == "1" ]]; then
	echo "=== verify: mod zephyr qemu (skipped — SKIP_MOD_ZEPHYR_QEMU=1) ==="
else
	echo "=== verify: mod zephyr qemu ==="
	"${ROOT}/scripts/verify-mod-zephyr-qemu.sh"
fi

echo "mod verify: ok"
