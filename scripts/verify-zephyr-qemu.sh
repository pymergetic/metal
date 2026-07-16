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
	"verify: utils exit=0" \
	"[ERROR] t3_util_native: at/above floor (expected)" \
	"t23_pthread: worker wrote 42" \
	"verify: tmpfs-indep next open fail is expected" \
	"t15_tmpfs_read_other: open failed (expected)" \
	"verify: process killing t5_spin (expected Exception follows)" \
	"verify: sockets tcp/udp/ipv6/dns ok" \
	"verify: scripted exit=0"

grep -q "runtime: target=zephyr" "${OUT}"
grep -qF -- "verify: utils exit=0" "${OUT}"
grep -qF -- "[ERROR] t3_util_native: at/above floor (expected)" "${OUT}"
grep -qF -- "t23_pthread: worker wrote 42" "${OUT}"
grep -qF -- "verify: tmpfs-indep next open fail is expected" "${OUT}"
grep -qF -- "t15_tmpfs_read_other: open failed (expected)" "${OUT}"
grep -qF -- "verify: process killing t5_spin (expected Exception follows)" "${OUT}"
grep -qF -- "verify: sockets tcp/udp/ipv6/dns ok" "${OUT}"
grep -qF -- "verify: scripted exit=0" "${OUT}"
grep -qxF -- "te" "${OUT}" && { echo "FAIL: O_TRUNC left populate tail in tmpfs read" >&2; exit 1; }
echo "zephyr qemu verify: ok"
