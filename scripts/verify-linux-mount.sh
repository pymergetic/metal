#!/usr/bin/env bash
# Multi-mount proof for pm_metal_mount — /etc/fstab (Stage B) mounting a
# second host dir onto /data, guest WASI I/O actually resolving through it
# (not just the root mount), and --mount= CLI sugar overriding a
# conflicting fstab line (last-mount-at-a-path-wins). See docs/MOUNT.md.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${ROOT}/scripts/build-mod.sh"
"${ROOT}/scripts/build-linux.sh"

RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

VFS_ROOT="$(mktemp -d)"
DATA_DIR="$(mktemp -d)"
CLI_DIR="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}" "${DATA_DIR}" "${CLI_DIR}"' EXIT

printf 'hello from vfs root\n' > "${VFS_ROOT}/README"
mkdir -p "${VFS_ROOT}/mods"
cp "${ROOT}/build/mods/t0_hello.wasm" "${ROOT}/build/mods/t1_read.wasm" \
	"${ROOT}/build/mods/t1y_mount_data.wasm" \
	"${VFS_ROOT}/mods/"

printf 'hello from data mount (fstab)\n' > "${DATA_DIR}/README"
printf 'hello from data mount (cli override)\n' > "${CLI_DIR}/README"

# --- Run 1: fstab-only — /data comes from DATA_DIR, via Stage B. ---
mkdir -p "${VFS_ROOT}/etc"
cat > "${VFS_ROOT}/etc/fstab" <<EOF
# <source>       <target>   <fstype>   <options>
${DATA_DIR}      /data      hostdir    rw
EOF

OUT1="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --rootfs=hostdir:"${VFS_ROOT}" \
	/mods/t0_hello.wasm \
	/mods/t1_read.wasm \
	/mods/t1y_mount_data.wasm)"

echo "${OUT1}"

echo "${OUT1}" | grep -qE "t0_hello\.wasm: exit=0" \
	|| { echo "FAIL: --rootfs=hostdir:<dir> did not behave like --vfs-root=<dir>" >&2; exit 1; }
echo "${OUT1}" | grep -q "hello from vfs root" \
	|| { echo "FAIL: root mount (Stage A) not resolving guest WASI I/O" >&2; exit 1; }
echo "${OUT1}" | grep -q "hello from data mount (fstab)" \
	|| { echo "FAIL: /etc/fstab (Stage B) did not mount /data, or guest I/O didn't resolve through it" >&2; exit 1; }
echo "${OUT1}" | grep -qE "t1y_mount_data\.wasm: exit=0" \
	|| { echo "FAIL: t1y_mount_data did not exit 0 against the fstab-mounted /data" >&2; exit 1; }

# --- Run 2: same fstab, plus --mount= overriding the same /data target —
# last-mount-at-a-path-wins, so this must now read CLI_DIR instead. ---
OUT2="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --rootfs=hostdir:"${VFS_ROOT}" \
	--mount=hostdir:"${CLI_DIR}":/data \
	/mods/t1y_mount_data.wasm)"

echo "${OUT2}"

echo "${OUT2}" | grep -q "hello from data mount (cli override)" \
	|| { echo "FAIL: --mount= did not override the fstab-mounted /data (last-mount-wins)" >&2; exit 1; }
echo "${OUT2}" | grep -q "hello from data mount (fstab)" \
	&& { echo "FAIL: --mount= override did not actually replace the old /data backing" >&2; exit 1; }

# --- Deprecated alias: --vfs-root= must still behave exactly like
# --rootfs=hostdir:<dir> (Phase 1's own "behavior identical to today"). ---
OUT3="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --vfs-root="${VFS_ROOT}" \
	/mods/t1_read.wasm)"

echo "${OUT3}" | grep -q "hello from vfs root" \
	|| { echo "FAIL: deprecated --vfs-root= alias regressed" >&2; exit 1; }

echo "verify-linux-mount: OK"
