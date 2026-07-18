#!/usr/bin/env bash
# NuttX sim default suite — same guest paths as linux/zephyr (no short renames).
# Guest content from embedded lz4 packages (pkg_apply_all), not hostfs install.
# tmpfs / populate / proc / process+sockets: not yet on this port (see scripts/verify).
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"
NUTTX="${ROOT}/build/nuttx/sim/nuttx"
TIMEOUT_SECS="${NUTTX_VERIFY_TIMEOUT:-180}"
if [[ -f "${ROOT}/.venv/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${ROOT}/.venv/bin/activate"
fi

"${ROOT}/scripts/build.d/port/nuttx/sim.sh"

if [[ ! -x "${NUTTX}" ]]; then
	echo "missing ${NUTTX}" >&2
	exit 1
fi

# Real file copies (not symlinks): NuttX hostfs readlink() on a host
# symlink returns EINVAL (22).
VFS_ROOT="$(mktemp -d)"
NSH_SCRIPT="$(mktemp)"
LOG="$(mktemp)"
trap 'rm -rf "${VFS_ROOT}" "${NSH_SCRIPT}" "${LOG}"' EXIT

printf 'hello from vfs root\n' >"${VFS_ROOT}/README"
# Pre-create for WASI preopen workaround (same as linux/python.sh): nested
# stdlib imports need /lib/python3.14 as its own map_dir, not only under /.
mkdir -p "${VFS_ROOT}/lib/python3.14"

PM="pm_metal --vfs-root=/v"
PY="pm_metal --vfs-root=/v --mount=hostdir:/v/lib/python3.14:/lib/python3.14 --env=PYTHONHOME=/"
{
	echo "mount -t hostfs -o fs=${VFS_ROOT} /v"
	echo "${PM} /mods/tests/t0_hello.wasm"
	echo "${PM} /mods/tests/t1_read.wasm"
	echo "${PM} /mods/tests/t3_util_native.wasm"
	echo "${PM} /mods/tests/t4_getpid.wasm"
	echo "${PM} /mods/tests/t9_multimod_app.wasm"
	echo "${PM} /mods/tests/t23_pthread.wasm"
	echo "${PM} /mods/tests/t31_net_util.wasm"
	if [[ "${PM_METAL_APP_PYTHON}" == "1" ]]; then
		echo "${PY} -- /mods/apps/python.wasm --version"
		echo "${PY} -- /mods/apps/python.wasm /mods/apps/pm-test.py"
	fi
	echo "poweroff"
} >"${NSH_SCRIPT}"

echo "=== nsh script ==="
cat "${NSH_SCRIPT}"
echo "=== run: nuttx sim (timeout ${TIMEOUT_SECS}s) ==="

set +e
timeout "${TIMEOUT_SECS}" "${NUTTX}" <"${NSH_SCRIPT}" >"${LOG}" 2>&1
rc=$?
set -e
cat "${LOG}"

if [[ "${rc}" -eq 124 ]]; then
	echo "FAIL: nuttx sim timed out after ${TIMEOUT_SECS}s" >&2
	exit 1
fi

OUT="$(cat "${LOG}")"
pm_suite_expect_scripted
if [[ "${PM_METAL_APP_PYTHON}" == "1" ]]; then
	pm_suite_expect_python python
fi

echo "verify-nuttx-sim: ok"
