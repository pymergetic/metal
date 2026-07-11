#!/usr/bin/env bash
# Zephyr qemu_x86_64 — build + QEMU run (real target smoke).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_BUILD="${ROOT}/build/zephyr/qemu_x86_64"
QEMU_TIMEOUT="${QEMU_TIMEOUT:-20}"

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

echo "=== build: qemu_x86_64 (engine-only) ==="
"${ROOT}/scripts/build-zephyr-qemu.sh"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "qemu_x86_64: run skipped (qemu-system-x86_64 not in PATH; build ok)"
	echo "zephyr qemu verify: ok (build only)"
	exit 0
fi

OUT_QEMU="$(mktemp)"
cleanup() { rm -f "${OUT_QEMU}"; }
trap cleanup EXIT

echo
echo "=== run: qemu_x86_64 (up to ${QEMU_TIMEOUT}s) ==="
pm_zephyr_qemu_run_boot_smoke "${QEMU_BUILD}" "${OUT_QEMU}" "${QEMU_TIMEOUT}" || true
echo

pm_zephyr_qemu_boot_ok "${OUT_QEMU}"

echo "zephyr qemu verify: ok"
