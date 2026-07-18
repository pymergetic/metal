#!/usr/bin/env bash
# Virtual /proc proof — hook-on-open, no /tmp materialization.
# See docs/MOUNT.md Phase 6b.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"

"${ROOT}/scripts/build.d/port/linux/default.sh"

RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

VFS_ROOT="$(mktemp -d)"
DATA_DIR="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}" "${DATA_DIR}"' EXIT

mkdir -p "${VFS_ROOT}/etc"
printf 'hello\n' > "${DATA_DIR}/README"

cat > "${VFS_ROOT}/etc/fstab" <<EOF
${DATA_DIR}      /data      hostdir    rw
EOF

# Must not create the old materialize path.
rm -rf /tmp/pm_metal_proc

OUT="$("${RUNTIME}" --rootfs=hostdir:"${VFS_ROOT}" \
	/mods/tests/t21_proc_mounts.wasm)"

echo "${OUT}"

if [ -d /tmp/pm_metal_proc ]; then
	echo "FAIL: /tmp/pm_metal_proc exists — materialization path still active" >&2
	exit 1
fi

echo "${OUT}" | grep -qE '^[^ ]+ / hostdir ' \
	|| { echo "FAIL: /proc/mounts missing root hostdir line" >&2; exit 1; }
echo "${OUT}" | grep -qE '^[^ ]+ /data hostdir ' \
	|| { echo "FAIL: /proc/mounts missing /data hostdir line" >&2; exit 1; }
echo "${OUT}" | grep -qE '^proc /proc proc ' \
	|| { echo "FAIL: /proc/mounts missing proc /proc line" >&2; exit 1; }
echo "${OUT}" | grep -qE 'nodev[[:space:]]+hostdir' \
	|| { echo "FAIL: /proc/filesystems missing hostdir" >&2; exit 1; }
echo "${OUT}" | grep -qE 'nodev[[:space:]]+tmpfs' \
	|| { echo "FAIL: /proc/filesystems missing tmpfs" >&2; exit 1; }
echo "${OUT}" | grep -qE 'nodev[[:space:]]+proc' \
	|| { echo "FAIL: /proc/filesystems missing proc" >&2; exit 1; }
echo "${OUT}" | grep -qE 'pymergetic-metal .* \(linux wasm32-wasi\)' \
	|| { echo "FAIL: /proc/version missing Metal identity" >&2; exit 1; }
echo "${OUT}" | grep -qE 'MemTotal:[[:space:]]+[1-9][0-9]* kB' \
	|| { echo "FAIL: /proc/meminfo missing non-zero MemTotal" >&2; exit 1; }
echo "${OUT}" | grep -qE 'BytecodeTotal:[[:space:]]+[1-9][0-9]* kB' \
	|| { echo "FAIL: /proc/meminfo missing non-zero BytecodeTotal" >&2; exit 1; }
echo "${OUT}" | grep -qE 'model name[[:space:]]*:[[:space:]]*pymergetic-metal wasm32' \
	|| { echo "FAIL: /proc/cpuinfo missing Metal wasm model" >&2; exit 1; }
echo "${OUT}" | grep -qE 't21_proc_mounts\.wasm' \
	|| { echo "FAIL: /proc/self/cmdline missing guest argv0" >&2; exit 1; }
echo "${OUT}" | grep -qE '^[0-9]+\.[0-9]{2} 0\.00$' \
	|| { echo "FAIL: /proc/uptime missing Metal uptime" >&2; exit 1; }
echo "${OUT}" | grep -qE 'PID=[0-9]+' \
	|| { echo "FAIL: /proc/self/environ missing guest PID=" >&2; exit 1; }
echo "${OUT}" | grep -qE "t21_proc_mounts\.wasm: exit=0" \
	|| { echo "FAIL: t21_proc_mounts did not exit 0" >&2; exit 1; }

echo "verify-linux-proc: OK"
