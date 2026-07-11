#!/usr/bin/env bash
# Zephyr native_sim — build + run (fast local smoke).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NATIVE_BUILD="${ROOT}/build/zephyr/native_sim"
NATIVE_TIMEOUT="${NATIVE_TIMEOUT:-12}"

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

echo "=== build: native_sim/native/64 (engine-only) ==="
"${ROOT}/scripts/build-zephyr-native-sim.sh"

OUT_NATIVE="$(mktemp)"
cleanup() { rm -f "${OUT_NATIVE}"; }
trap cleanup EXIT

echo
echo "=== run: native_sim/native/64 (${NATIVE_TIMEOUT}s) ==="
pm_zephyr_run_with_timeout "${NATIVE_TIMEOUT}" \
	"${NATIVE_BUILD}/zephyr/zephyr.exe" >"${OUT_NATIVE}" 2>&1 || true

echo "=== native_sim output ==="
cat "${OUT_NATIVE}"
echo

grep -q "runtime: target=zephyr" "${OUT_NATIVE}"
grep -q "pymergetic orchestrator" "${OUT_NATIVE}" || grep -q "published bootstrap" "${OUT_NATIVE}"

echo "zephyr native_sim verify: ok"
