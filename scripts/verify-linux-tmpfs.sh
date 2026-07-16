#!/usr/bin/env bash
# tmpfs fstype proof for pm_metal_mount — /etc/fstab mounting two named
# tmpfs sources ("scratch" twice, at two different targets; "other" once),
# guest WASI I/O writing from one mod and reading back from a separate one
# on the *same* tmpfs mount, a repeated fstab line for the same name
# reusing rather than re-creating its backing, two differently-named tmpfs
# sources staying independent, and the backing actually gone (not just
# logically unreachable) after shutdown. See docs/MOUNT.md "Named
# ramdisks", scripts/verify-linux-mount.sh for the sibling Phase 2 proof.
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
	echo "SKIP: /dev/shm not present on this host, tmpfs fstype has nothing to test against" >&2
	exit 0
fi

VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}"' EXIT

mkdir -p "${VFS_ROOT}/mods" "${VFS_ROOT}/etc"
cp "${ROOT}/build/mods/t12_tmpfs_write.wasm" \
	"${ROOT}/build/mods/t13_tmpfs_read.wasm" \
	"${ROOT}/build/mods/t14_tmpfs_read_alt.wasm" \
	"${ROOT}/build/mods/t15_tmpfs_read_other.wasm" \
	"${VFS_ROOT}/mods/"

cat > "${VFS_ROOT}/etc/fstab" <<EOF
# <source>   <target>    <fstype>   <options>   <dump>  <pass>
scratch      /scratch    tmpfs      rw          0       0
scratch      /scratchB   tmpfs      rw          0       0
other        /other      tmpfs      rw          0       0
EOF

shm_leftover_count() {
	# nullglob-safe: a bare unmatched glob would otherwise count as 1 "file".
	shopt -s nullglob
	local -a matches=(/dev/shm/pm_metal_tmpfs_*)
	shopt -u nullglob
	echo "${#matches[@]}"
}

BEFORE="$(shm_leftover_count)"

# --- Run 1: write (t12), read back same mount (t13), read back via a
# second fstab line naming the same source (t14, reuse), read a
# differently-named source expecting failure (t15, independence). All in
# one boot, one already-mounted namespace, matching how these mounts are
# actually meant to be used. t15's own expected non-zero exit makes the
# whole run_scripted() call return non-zero too (see app.c) — `|| true`
# so that expected failure doesn't trip this script's own set -e. ---
OUT1="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --rootfs=hostdir:"${VFS_ROOT}" \
	/mods/t12_tmpfs_write.wasm \
	/mods/t13_tmpfs_read.wasm \
	/mods/t14_tmpfs_read_alt.wasm \
	/mods/t15_tmpfs_read_other.wasm)" || true

echo "${OUT1}"

echo "${OUT1}" | grep -qE "t12_tmpfs_write\.wasm: exit=0" \
	|| { echo "FAIL: write into tmpfs-mounted /scratch failed" >&2; exit 1; }
echo "${OUT1}" | grep -qE "t13_tmpfs_read\.wasm: exit=0" \
	|| { echo "FAIL: read back from same tmpfs mount, different mod, failed" >&2; exit 1; }
echo "${OUT1}" | grep -q "t13_tmpfs_read: hello from tmpfs" \
	|| { echo "FAIL: read back from /scratch did not see t12's write" >&2; exit 1; }
echo "${OUT1}" | grep -qE "t14_tmpfs_read_alt\.wasm: exit=0" \
	|| { echo "FAIL: second fstab line for the same tmpfs name ('scratch' at /scratchB) did not resolve" >&2; exit 1; }
echo "${OUT1}" | grep -q "t14_tmpfs_read_alt: hello from tmpfs" \
	|| { echo "FAIL: repeated fstab name did not reuse the already-established backing (re-created instead?)" >&2; exit 1; }
echo "${OUT1}" | grep -qE "t15_tmpfs_read_other\.wasm: exit=0" \
	|| { echo "FAIL: differently-named tmpfs source ('other') was not independent from 'scratch'" >&2; exit 1; }
echo "${OUT1}" | grep -q "t15_tmpfs_read_other: open failed (expected)" \
	|| { echo "FAIL: differently-named tmpfs source ('other') was not independent from 'scratch'" >&2; exit 1; }

# --- Run 2: fresh process, same fstab — a brand-new "scratch" tmpfs is
# established, with none of run 1's content. Proves the previous run's
# backing was actually torn down at shutdown, not just orphaned. ---
OUT2="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --rootfs=hostdir:"${VFS_ROOT}" \
	/mods/t13_tmpfs_read.wasm)" || true

echo "${OUT2}"

echo "${OUT2}" | grep -q "t13_tmpfs_read: open failed" \
	|| { echo "FAIL: a fresh process still saw the previous run's tmpfs content — not torn down at shutdown" >&2; exit 1; }

AFTER="$(shm_leftover_count)"
if [ "${AFTER}" != "${BEFORE}" ]; then
	echo "FAIL: /dev/shm/pm_metal_tmpfs_* leftover count changed (${BEFORE} -> ${AFTER}) — a tmpfs backing directory leaked" >&2
	exit 1
fi

echo "verify-linux-tmpfs: OK"
