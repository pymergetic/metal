#!/usr/bin/env bash
# Build all runtime engines: linux + zephyr native_sim + zephyr qemu.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKIP_LINUX="${SKIP_LINUX:-0}"
SKIP_ZEPHYR_NATIVE_SIM="${SKIP_ZEPHYR_NATIVE_SIM:-0}"
SKIP_ZEPHYR_QEMU="${SKIP_ZEPHYR_QEMU:-0}"

if [[ "${SKIP_LINUX}" != "1" ]]; then
	echo "=== build: linux ==="
	"${ROOT}/scripts/build-linux.sh"
	echo
fi

if [[ "${SKIP_ZEPHYR_NATIVE_SIM}" != "1" ]]; then
	echo "=== build: zephyr native_sim ==="
	"${ROOT}/scripts/build-zephyr-native-sim.sh"
	echo
fi

if [[ "${SKIP_ZEPHYR_QEMU}" != "1" ]]; then
	echo "=== build: zephyr qemu ==="
	"${ROOT}/scripts/build-zephyr-qemu.sh"
	echo
fi

echo "engines: ok"
