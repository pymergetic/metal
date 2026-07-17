#!/usr/bin/env bash
# Guest mount()/umount() proof — privileged mods (MOUNT marker) register a
# tmpfs at /dyn; same process can use it (live remount); a following process
# can also write/read it; umount then makes it disappear for the next process.
# Also proves --allow-guest-mount deny and ".." target rejection.
# See docs/MOUNT.md.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${ROOT}/scripts/build-mod.sh"
"${ROOT}/scripts/build-linux.sh"

RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

if [ ! -d /dev/shm ]; then
	echo "SKIP: /dev/shm not present — guest tmpfs mount has nothing to back it" >&2
	exit 0
fi

VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}"' EXIT

mkdir -p "${VFS_ROOT}/mods"
cp "${ROOT}/build/mods/t17_sys_mount.wasm" \
	"${ROOT}/build/mods/t18_sys_use.wasm" \
	"${ROOT}/build/mods/t19_sys_umount.wasm" \
	"${ROOT}/build/mods/t20_sys_gone.wasm" \
	"${ROOT}/build/mods/t29_sys_mount_denied.wasm" \
	"${ROOT}/build/mods/t30_sys_mount_dotdot.wasm" \
	"${VFS_ROOT}/mods/"

# --- Deny path: no --allow-guest-mount ---
OUT_DENY="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --rootfs=hostdir:"${VFS_ROOT}" \
	/mods/t29_sys_mount_denied.wasm)" || true

echo "${OUT_DENY}"

echo "${OUT_DENY}" | grep -qE "t29_sys_mount_denied\.wasm: exit=0" \
	|| { echo "FAIL: mount without --allow-guest-mount should exit 0 (denied)" >&2; exit 1; }
echo "${OUT_DENY}" | grep -q "t29_sys_mount_denied: mount rejected (expected)" \
	|| { echo "FAIL: expected denial message from t29" >&2; exit 1; }

# --- Allowed path: happy mount/umount + ".." rejection ---
OUT="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --rootfs=hostdir:"${VFS_ROOT}" \
	--allow-guest-mount \
	/mods/t30_sys_mount_dotdot.wasm \
	/mods/t17_sys_mount.wasm \
	/mods/t18_sys_use.wasm \
	/mods/t19_sys_umount.wasm \
	/mods/t20_sys_gone.wasm)" || true

echo "${OUT}"

echo "${OUT}" | grep -qE "t30_sys_mount_dotdot\.wasm: exit=0" \
	|| { echo "FAIL: mount target with .. should be rejected (exit 0)" >&2; exit 1; }
echo "${OUT}" | grep -q "t30_sys_mount_dotdot: mount rejected (expected)" \
	|| { echo "FAIL: expected .. rejection message from t30" >&2; exit 1; }
echo "${OUT}" | grep -qE "t17_sys_mount\.wasm: exit=0" \
	|| { echo "FAIL: guest mount() + same-process use failed" >&2; exit 1; }
echo "${OUT}" | grep -q "t17_sys_mount: hello from same-process mount" \
	|| { echo "FAIL: same-process open/read after mount() failed" >&2; exit 1; }
echo "${OUT}" | grep -qE "t18_sys_use\.wasm: exit=0" \
	|| { echo "FAIL: following process could not use the newly mounted /dyn" >&2; exit 1; }
echo "${OUT}" | grep -q "t18_sys_use: hello from guest mount" \
	|| { echo "FAIL: readback through guest-mounted tmpfs failed" >&2; exit 1; }
echo "${OUT}" | grep -qE "t19_sys_umount\.wasm: exit=0" \
	|| { echo "FAIL: guest umount() failed" >&2; exit 1; }
echo "${OUT}" | grep -qE "t20_sys_gone\.wasm: exit=0" \
	|| { echo "FAIL: /dyn still visible after umount" >&2; exit 1; }
echo "${OUT}" | grep -q "t20_sys_gone: open failed" \
	|| { echo "FAIL: /dyn still visible after umount" >&2; exit 1; }

echo "verify-linux-sys-mount: OK"
