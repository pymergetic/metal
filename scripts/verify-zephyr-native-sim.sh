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
	"verify: utils exit=0" \
	"[ERROR] t3_util_native: at/above floor (expected)" \
	"t23_pthread: worker wrote 42" \
	"verify: tmpfs exit=0" \
	"verify: tmpfs-indep next open fail is expected" \
	"t15_tmpfs_read_other: open failed (expected)" \
	"verify: tmpfs-indep expected-fail ok" \
	"verify: populate exit=0" \
	"verify: proc exit=0" \
	"verify: multimod exit=0" \
	"verify: process t4_getpid" \
	"verify: process killing t5_spin (expected Exception follows)" \
	"verify: process t5_spin killed" \
	"verify: sockets tcp/udp/ipv6/dns ok" \
	"verify: scripted exit=0"

grep -q "runtime: target=zephyr" "${OUT_NATIVE}"
grep -q "verify: basic exit=0" "${OUT_NATIVE}"
grep -q "verify: utils exit=0" "${OUT_NATIVE}"
grep -qF -- "[ERROR] t3_util_native: at/above floor (expected)" "${OUT_NATIVE}"
grep -qF -- "t23_pthread: worker wrote 42" "${OUT_NATIVE}"
grep -qF -- "verify: tmpfs exit=0" "${OUT_NATIVE}"
grep -qF -- "verify: tmpfs-indep next open fail is expected" "${OUT_NATIVE}"
grep -qF -- "t15_tmpfs_read_other: open failed (expected)" "${OUT_NATIVE}"
grep -qF -- "verify: tmpfs-indep expected-fail ok" "${OUT_NATIVE}"
grep -qF -- "verify: populate exit=0" "${OUT_NATIVE}"
grep -qF -- "verify: proc exit=0" "${OUT_NATIVE}"
grep -qF -- "verify: multimod exit=0" "${OUT_NATIVE}"
grep -qF -- "verify: process t4_getpid" "${OUT_NATIVE}"
grep -qF -- "verify: process killing t5_spin (expected Exception follows)" "${OUT_NATIVE}"
grep -qF -- "verify: process t5_spin killed" "${OUT_NATIVE}"
grep -qF -- "verify: sockets tcp/udp/ipv6/dns ok" "${OUT_NATIVE}"
grep -qF -- "verify: scripted exit=0" "${OUT_NATIVE}"
# Truncation after populate overwrite — no leftover "te" from populate.
grep -qxF -- "te" "${OUT_NATIVE}" && { echo "FAIL: O_TRUNC left populate tail in tmpfs read" >&2; exit 1; }
echo "zephyr native_sim verify: ok"
