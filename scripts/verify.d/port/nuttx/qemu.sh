#!/usr/bin/env bash
# NuttX qemu-intel64 — core suite via nsh over serial (embedded packages).
# Skips t31 (HTTPS needs mbedTLS/curl — sim-only host curl). See src/nuttx/README.md.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/nuttx-qemu.sh"

NUTTX_KERNEL="${ROOT}/build/nuttx/qemu/nuttx"
TIMEOUT_SECS="${NUTTX_QEMU_VERIFY_TIMEOUT:-600}"

"${ROOT}/scripts/build.d/port/nuttx/qemu.sh"

if [[ ! -f "${NUTTX_KERNEL}" ]]; then
	echo "missing ${NUTTX_KERNEL}" >&2
	exit 1
fi

NSH_SCRIPT="$(mktemp)"
OUT_FILE="$(mktemp)"
trap 'rm -f "${NSH_SCRIPT}" "${OUT_FILE}"' EXIT

# Stage A still requires hostdir (see docs/MOUNT.md). On qemu that is a
# directory on NuttX tmpfs — packages extract into it on first pm_metal.
# Use /data (not /tmp): some boards pre-mount a tiny tmpfs on /tmp.
ROOTFS="/data/pm_metal_root"
PM="pm_metal --vfs-root=${ROOTFS}"
# Separate preopen for stdlib (same WASI map_dir workaround as nuttx sim / linux).
PY="pm_metal --vfs-root=${ROOTFS} --mount=hostdir:${ROOTFS}/lib/python3.14:/lib/python3.14 --env=PYTHONHOME=/"
{
	echo "mkdir -p /data"
	echo "mount -t tmpfs /data"
	echo "mkdir -p ${ROOTFS}"
	echo "mkdir -p ${ROOTFS}/lib/python3.14"
	echo "echo hello from vfs root > ${ROOTFS}/README"
	echo "${PM} /mods/tests/t0_hello.wasm"
	echo "${PM} /mods/tests/t1_read.wasm"
	echo "${PM} /mods/tests/t3_util_native.wasm"
	echo "${PM} /mods/tests/t4_getpid.wasm"
	echo "${PM} /mods/tests/t9_multimod_app.wasm"
	echo "${PM} /mods/tests/t23_pthread.wasm"
	if [[ "${PM_METAL_APP_PYTHON}" == "1" ]]; then
		echo "${PY} -- /mods/apps/python.wasm --version"
		echo "${PY} -- /mods/apps/python.wasm /mods/apps/pm-test.py"
	fi
	# No poweroff builtin on qemu-intel64:nsh; harness stops qemu on markers.
} >"${NSH_SCRIPT}"

MARKERS=(
	"t0_hello"
	"hello from vfs root"
	"t3_util_native: tar+lz4 round-trip ok"
	"t23_pthread: worker wrote 42"
)
if [[ "${PM_METAL_APP_PYTHON}" == "1" ]]; then
	MARKERS+=("Python 3.14" "pm-test: ok")
fi

echo "=== nsh script ==="
cat "${NSH_SCRIPT}"

pm_nuttx_qemu_run_smoke "${NUTTX_KERNEL}" "${NSH_SCRIPT}" "${OUT_FILE}" "${TIMEOUT_SECS}" \
	"${MARKERS[@]}"

# SeaBIOS/CSI embed NULs; bash $(cat) truncates at the first one.
OUT="$(tr -d '\000' <"${OUT_FILE}")"
pm_suite_expect_scripted_no_net
if [[ "${PM_METAL_APP_PYTHON}" == "1" ]]; then
	pm_suite_expect_python python
fi

echo "verify-nuttx-qemu: ok"
