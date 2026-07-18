#!/usr/bin/env bash
# Concurrency proof for pm_metal_runtime — builds pm-linux-thread-stress
# (tests/thread_stress_test.c) and pm-wamr-vmlib with ThreadSanitizer
# in a separate build dir, runs it, and fails if TSan reports anything.
# See docs/RUNTIME.md "Concurrency" for the threading contract this checks.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"
BUILD="${ROOT}/build/linux/tsan"
JOBS="$(nproc 2>/dev/null || echo 4)"

pm_guest_pkgs_compose

# WAMR_DISABLE_HW_BOUND_CHECK: WAMR's default hardware bounds check mmaps a
# large guard region per instance for trap-based OOB detection; TSan's own
# shadow-memory layout doesn't tolerate that ("unexpected memory mapping"
# abort), so sanitizer builds need the software bounds check path instead.
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_C_FLAGS="-fsanitize=thread -g -O1" \
	-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
	-DWAMR_DISABLE_HW_BOUND_CHECK=1
cmake --build "${BUILD}" --target pm-linux-thread-stress -j"${JOBS}"

RUNTIME="${BUILD}/pm-linux-thread-stress"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

VFS_ROOT="$(mktemp -d)"
LOG="$(mktemp)"
trap 'rm -f "${LOG}"; rm -rf "${VFS_ROOT}"' EXIT

# t1_read.wasm opens /README — without it every t1_read run legitimately
# fails open() and prints "t1_read: open failed" (see mods/t1_read/main.c);
# that's the missing fixture talking, not a lock/race/OOM symptom.
printf 'hello from vfs root\n' > "${VFS_ROOT}/README"

# setarch -R (disable ASLR): on this host/kernel/glibc combo, TSan's runtime
# aborts at startup ("unexpected memory mapping") under full ASLR — a known
# class of TSan/address-space-layout incompatibility, unrelated to anything
# in this codebase. Disabling ASLR for this one process works around it.
#
# TSAN_OPTIONS: exit 66 on the first race so the script fails loudly instead
# of relying on grep-ing free-form warning text. set +e: capture the exit
# code ourselves rather than letting `set -e` abort before we can inspect it.
set +e
PM_METAL_TEST_VFS_ROOT="${VFS_ROOT}" \
	setarch -R env TSAN_OPTIONS="exitcode=66 halt_on_error=1" \
	"${RUNTIME}" >"${LOG}" 2>&1
RC=$?
set -e

cat "${LOG}"

if [ "${RC}" -eq 66 ]; then
	echo "FAIL: ThreadSanitizer reported a data race — see log above" >&2
	exit 1
fi
if [ "${RC}" -ne 0 ]; then
	echo "FAIL: pm-linux-thread-stress exited ${RC}" >&2
	exit 1
fi

grep -q "thread_stress: OK" "${LOG}" \
	|| { echo "FAIL: missing completion marker" >&2; exit 1; }

echo "verify-linux-threads: OK (no data races under ThreadSanitizer)"
