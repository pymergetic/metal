#!/usr/bin/env bash
# Zephyr qemu_x86_64 — build + boot smoke (same markers as native_sim).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_BUILD="${ROOT}/build/zephyr/qemu_x86_64"
QEMU_TIMEOUT="${QEMU_TIMEOUT:-120}"

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

"${ROOT}/scripts/gen-zephyr-mods-embed.sh"
"${ROOT}/scripts/build-zephyr-qemu.sh"

OUT="$(mktemp)"
trap 'rm -f "${OUT}"' EXIT

pm_zephyr_qemu_run_smoke "${QEMU_BUILD}" "${OUT}" "${QEMU_TIMEOUT}" \
	"runtime: target=zephyr" \
	"verify: basic exit=0" \
	"verify: scripted exit=0"

grep -q "runtime: target=zephyr" "${OUT}"
grep -q "verify: scripted exit=0" "${OUT}"
echo "zephyr qemu verify: ok"
