#!/usr/bin/env bash
# Symmetric runtime verification.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKIP_ZEPHYR="${SKIP_ZEPHYR:-0}"
SKIP_ZEPHYR_NATIVE_SIM="${SKIP_ZEPHYR_NATIVE_SIM:-0}"
SKIP_ZEPHYR_QEMU="${SKIP_ZEPHYR_QEMU:-0}"
SKIP_MOD="${SKIP_MOD:-0}"

echo "=== verify: linux ==="
"${ROOT}/scripts/verify-linux.sh"
echo

if [[ "${SKIP_ZEPHYR}" == "1" ]]; then
	echo "=== verify: zephyr (skipped — SKIP_ZEPHYR=1) ==="
else
	if [[ "${SKIP_ZEPHYR_NATIVE_SIM}" == "1" ]]; then
		echo "=== verify: zephyr native_sim (skipped — SKIP_ZEPHYR_NATIVE_SIM=1) ==="
	else
		echo "=== verify: zephyr native_sim ==="
		"${ROOT}/scripts/verify-zephyr-native-sim.sh"
	fi
	echo
	if [[ "${SKIP_ZEPHYR_QEMU}" == "1" ]]; then
		echo "=== verify: zephyr qemu (skipped — SKIP_ZEPHYR_QEMU=1) ==="
	else
		echo "=== verify: zephyr qemu ==="
		"${ROOT}/scripts/verify-zephyr-qemu.sh"
	fi
fi
echo

if [[ "${SKIP_MOD}" == "1" ]]; then
	echo "=== verify: mod (skipped — SKIP_MOD=1) ==="
else
	echo "=== verify: mod ==="
	"${ROOT}/scripts/verify-mod.sh"
fi
echo

echo "verify: ok"
