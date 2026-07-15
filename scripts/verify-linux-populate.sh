#!/usr/bin/env bash
# Boot-time populate proof — pack a guest-path tree into an embed .c, link it
# into pm-linux-runtime, Stage B mounts tmpfs at /scratch, populate_all()
# extracts scratch/hello.txt against "/", guest t16_populate_read sees it.
# See docs/MOUNT.md Phase 4.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${ROOT}/scripts/build-mod.sh"

FIXTURE="$(mktemp -d)"
EMBED="${ROOT}/build/linux/runtime/populate_embed.c"
trap 'rm -rf "${FIXTURE}"' EXIT

mkdir -p "${FIXTURE}/scratch"
printf 'hello from populate\n' > "${FIXTURE}/scratch/hello.txt"

mkdir -p "$(dirname "${EMBED}")"
"${ROOT}/scripts/pack-image.sh" "${FIXTURE}" "${EMBED}"

# Rebuild with the embed linked in (constructor registers the blob).
cmake -S "${ROOT}" -B "${ROOT}/build/linux/runtime" \
	-DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
	-DPM_METAL_POPULATE_EMBED="${EMBED}"
cmake --build "${ROOT}/build/linux/runtime" --target pm-linux-runtime -j"$(nproc 2>/dev/null || echo 4)"

RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${FIXTURE}" "${VFS_ROOT}"' EXIT

mkdir -p "${VFS_ROOT}/mods" "${VFS_ROOT}/etc"
cp "${ROOT}/build/mods/t16_populate_read.wasm" "${VFS_ROOT}/mods/"

cat > "${VFS_ROOT}/etc/fstab" <<EOF
# <source>   <target>    <fstype>   <options>
scratch      /scratch    tmpfs      rw
EOF

OUT="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --rootfs=hostdir:"${VFS_ROOT}" \
	/mods/t16_populate_read.wasm)"

echo "${OUT}"

echo "${OUT}" | grep -qE "t16_populate_read\.wasm: exit=0" \
	|| { echo "FAIL: populate extract or guest read failed" >&2; exit 1; }
echo "${OUT}" | grep -q "t16_populate_read: hello from populate" \
	|| { echo "FAIL: guest did not see populate_all() content under /scratch" >&2; exit 1; }

# --- Same again with lz4-compressed embed (util/lz4 block format). ---
"${ROOT}/scripts/pack-image.sh" --lz4 "${FIXTURE}" "${EMBED}"
cmake -S "${ROOT}" -B "${ROOT}/build/linux/runtime" \
	-DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
	-DPM_METAL_POPULATE_EMBED="${EMBED}"
cmake --build "${ROOT}/build/linux/runtime" --target pm-linux-runtime -j"$(nproc 2>/dev/null || echo 4)"

OUT_LZ4="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --rootfs=hostdir:"${VFS_ROOT}" \
	/mods/t16_populate_read.wasm)"

echo "${OUT_LZ4}"

echo "${OUT_LZ4}" | grep -q "t16_populate_read: hello from populate" \
	|| { echo "FAIL: lz4-compressed populate embed did not extract correctly" >&2; exit 1; }

# Rebuild without the embed so later verifies don't keep a stale fixture.
cmake -S "${ROOT}" -B "${ROOT}/build/linux/runtime" \
	-DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
	-DPM_METAL_POPULATE_EMBED=
cmake --build "${ROOT}/build/linux/runtime" --target pm-linux-runtime -j"$(nproc 2>/dev/null || echo 4)"
rm -f "${EMBED}"

echo "verify-linux-populate: OK"
