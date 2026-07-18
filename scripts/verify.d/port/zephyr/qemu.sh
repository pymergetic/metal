#!/usr/bin/env bash
# Zephyr qemu_x86_64 — default suite (same markers as native_sim).
#
# Accel: scripts/lib/zephyr-env.sh injects -accel kvm when /dev/kvm is R/W
# (PM_ZEPHYR_QEMU_ACCEL=auto|kvm|tcg). If TCG is used, add the user to group
# kvm and re-login:  sudo usermod -aG kvm "$USER"
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"
QEMU_BUILD="${ROOT}/build/zephyr/qemu_x86_64"
# CPython on FatFs is slow under TCG; KVM makes this much tighter. Headroom kept.
QEMU_TIMEOUT="${QEMU_TIMEOUT:-900}"

# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/zephyr-env.sh"

"${ROOT}/scripts/build.d/port/zephyr/embed.sh"
"${ROOT}/scripts/build.d/port/zephyr/qemu.sh"

OUT="$(mktemp)"
trap 'rm -f "${OUT}"' EXIT

pm_zephyr_qemu_run_smoke "${QEMU_BUILD}" "${OUT}" "${QEMU_TIMEOUT}" \
	"${PM_SUITE_ZEPHYR_MARKERS[@]}"

pm_suite_expect_zephyr_log "${OUT}"
echo "zephyr qemu verify: ok"
