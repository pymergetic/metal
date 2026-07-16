#!/usr/bin/env bash
# Zephyr native_sim — build + run mount/tmpfs/proc/process/multimod verify.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NATIVE_BUILD="${ROOT}/build/zephyr/native_sim"
NATIVE_TIMEOUT="${NATIVE_TIMEOUT:-120}"

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

echo "=== embed mods ==="
"${ROOT}/scripts/gen-zephyr-mods-embed.sh"

echo "=== build: native_sim/native/64 ==="
"${ROOT}/scripts/build-zephyr-native-sim.sh"

OUT_NATIVE="$(mktemp)"
trap 'rm -f "${OUT_NATIVE}"' EXIT

echo "=== run: native_sim (live; stop on success or ${NATIVE_TIMEOUT}s) ==="
# Stream live (unlike a silent redirect) and stop as soon as the suite
# finishes — native_sim has hung after "scripted exit=0" before.
pm_zephyr_run_smoke "${NATIVE_BUILD}/zephyr/zephyr.exe" "${OUT_NATIVE}" "${NATIVE_TIMEOUT}" \
	"runtime: target=zephyr" \
	"verify: basic exit=0" \
	"verify: tmpfs exit=0" \
	"verify: tmpfs-indep expected-fail ok" \
	"verify: populate exit=0" \
	"verify: proc exit=0" \
	"verify: multimod exit=0" \
	"verify: process t4_getpid" \
	"verify: process t5_spin killed" \
	"verify: scripted exit=0"

grep -q "runtime: target=zephyr" "${OUT_NATIVE}"
grep -q "verify: basic exit=0" "${OUT_NATIVE}"
grep -q "verify: tmpfs exit=0" "${OUT_NATIVE}"
grep -q "verify: tmpfs-indep expected-fail ok" "${OUT_NATIVE}"
grep -q "verify: populate exit=0" "${OUT_NATIVE}"
grep -q "verify: proc exit=0" "${OUT_NATIVE}"
grep -q "verify: multimod exit=0" "${OUT_NATIVE}"
grep -q "verify: process t4_getpid" "${OUT_NATIVE}"
grep -q "verify: process t5_spin killed" "${OUT_NATIVE}"
grep -q "verify: scripted exit=0" "${OUT_NATIVE}"
echo "zephyr native_sim verify: ok"
