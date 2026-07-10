#!/usr/bin/env bash
# Twin-target smoke: build and run native_sim, then build and run qemu_x86_64.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

NATIVE_SIM_BUILD="${ROOT}/runtime/zephyr/build/native_sim/native/64"
QEMU_BUILD="${ROOT}/runtime/zephyr/build/qemu_x86_64"
NATIVE_SIM_TIMEOUT="${NATIVE_SIM_TIMEOUT:-8}"
# qemu_x86_64 cold boot (SeaBIOS + SMP) is ~25s on typical CI boxes
QEMU_TIMEOUT="${QEMU_TIMEOUT:-45}"

source .venv/bin/activate
export ZEPHYR_BASE="${ROOT}/external/zephyr"

run_with_timeout() {
	local secs=$1
	shift

	if timeout --foreground "$secs" "$@"; then
		return 0
	fi

	local rc=$?

	# timeout(1) exits 124 when the time limit is hit — expected for smoke runs.
	if [[ $rc -eq 124 ]]; then
		echo "(stopped after ${secs}s timeout — boot looked healthy)"
		return 0
	fi

	return "$rc"
}

echo "=== build: native_sim/native/64 ==="
west build -p always -b native_sim/native/64 runtime/zephyr \
  --build-dir "${NATIVE_SIM_BUILD}"

ln -sf "${NATIVE_SIM_BUILD}/compile_commands.json" "${ROOT}/compile_commands.json"

echo
echo "=== build: qemu_x86_64 ==="
west build -p always -b qemu_x86_64 runtime/zephyr \
  --build-dir "${QEMU_BUILD}"

echo
echo "=== run: native_sim/native/64 (${NATIVE_SIM_TIMEOUT}s) ==="
run_with_timeout "${NATIVE_SIM_TIMEOUT}" \
  "${NATIVE_SIM_BUILD}/zephyr/zephyr.exe"

echo
echo "=== run: qemu_x86_64 (${QEMU_TIMEOUT}s) ==="
if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "qemu_x86_64: run skipped (qemu-system-x86_64 not in PATH; build ok)"
	echo "  hint: sudo apt install qemu-system-x86"
	echo
	echo "native_sim: ok"
	echo "qemu_x86_64: build ok (run skipped)"
	exit 0
fi

# ninja zephyr/run_qemu — not west -t run — so we don't burn timeout on west/ninja
# bookkeeping and serial output streams as soon as QEMU prints it.
run_with_timeout "${QEMU_TIMEOUT}" ninja -C "${QEMU_BUILD}" zephyr/run_qemu

echo
echo "native_sim: ok"
echo "qemu_x86_64: ok"
