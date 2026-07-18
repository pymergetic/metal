#!/usr/bin/env bash
# Zephyr native_sim — default suite (same markers as qemu).
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"
NATIVE_BUILD="${ROOT}/build/zephyr/native_sim"
NATIVE_TIMEOUT="${NATIVE_TIMEOUT:-600}"

# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/zephyr-env.sh"

echo "=== embed mods ==="
"${ROOT}/scripts/build.d/port/zephyr/embed.sh"

echo "=== build: native_sim/native/64 ==="
"${ROOT}/scripts/build.d/port/zephyr/native-sim.sh"

OUT_NATIVE="$(mktemp)"
trap 'rm -f "${OUT_NATIVE}"' EXIT

echo "=== run: native_sim (live; stop on success or ${NATIVE_TIMEOUT}s) ==="
# Stream live (unlike a silent redirect) and stop as soon as the suite
# finishes — native_sim has hung after "scripted exit=0" before.
pm_zephyr_run_smoke "${NATIVE_BUILD}/zephyr/zephyr.exe" "${OUT_NATIVE}" "${NATIVE_TIMEOUT}" \
	"${PM_SUITE_ZEPHYR_MARKERS[@]}"

pm_suite_expect_zephyr_log "${OUT_NATIVE}"
echo "zephyr native_sim verify: ok"
