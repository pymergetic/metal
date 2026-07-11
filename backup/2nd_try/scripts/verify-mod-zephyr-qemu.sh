#!/usr/bin/env bash
# Zephyr qemu_x86_64 — build with WAMR and run mod-smoke.wasm via wasm_host.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_MOD_BUILD="${ROOT}/build/zephyr/qemu_x86_64_mod"
QEMU_TIMEOUT="${QEMU_MOD_TIMEOUT:-45}"

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

if [[ ! -f "${ROOT}/mods/smoke/main.c" ]]; then
	echo "zephyr qemu mod verify: skipped (no mods/smoke/main.c)"
	exit 0
fi

echo "=== build: qemu_x86_64 (mod) ==="
"${ROOT}/scripts/build-zephyr-qemu-mod.sh"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "qemu_x86_64 mod: run skipped (qemu-system-x86_64 not in PATH; build ok)"
	echo "zephyr qemu mod verify: ok (build only)"
	exit 0
fi

OUT_QEMU="$(mktemp)"
cleanup() { rm -f "${OUT_QEMU}"; }
trap cleanup EXIT

echo
echo "=== run: qemu_x86_64 mod (up to ${QEMU_TIMEOUT}s) ==="
pm_zephyr_qemu_run_smoke "${QEMU_MOD_BUILD}" "${OUT_QEMU}" "${QEMU_TIMEOUT}" \
	"mod-smoke: machine_ram=" || true
echo

grep -q "mod-smoke: machine_ram=" "${OUT_QEMU}"

echo "zephyr qemu mod verify: ok"
