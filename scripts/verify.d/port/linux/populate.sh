#!/usr/bin/env bash
# Boot-time populate proof — pack a guest-path tree into an embed .c, link it
# into pm-linux-runtime, Stage B mounts tmpfs at /scratch, populate_all()
# extracts scratch/hello.txt against "/", guest t16_populate_read sees it.
# See docs/MOUNT.md Phase 4.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"

# Ensure named guest packages exist so t16.wasm is embedded alongside the
# anonymous populate fixture.
pm_guest_pkgs_compose

FIXTURE="$(mktemp -d)"
EMBED="${ROOT}/build/linux/runtime/populate_embed.c"
trap 'rm -rf "${FIXTURE}"' EXIT

mkdir -p "${FIXTURE}/scratch"
printf 'hello from populate\n' > "${FIXTURE}/scratch/hello.txt"

mkdir -p "$(dirname "${EMBED}")"
"${ROOT}/scripts/lib/pack-image.sh" "${FIXTURE}" "${EMBED}"

# Rebuild with the embed linked in (constructor registers the blob).
# Must reconfigure: -DPM_METAL_POPULATE_EMBED changes the linked blob.
PM_METAL_FORCE_CMAKE=1 pm_linux_cmake_configure "${ROOT}/build/linux/runtime" \
	-DPM_METAL_POPULATE_EMBED="${EMBED}"
pm_linux_cmake_build "${ROOT}/build/linux/runtime" pm-linux-runtime

RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${FIXTURE}" "${VFS_ROOT}"' EXIT

mkdir -p "${VFS_ROOT}/etc"

cat > "${VFS_ROOT}/etc/fstab" <<EOF
# <source>   <target>    <fstype>   <options>
scratch      /scratch    tmpfs      rw
EOF

OUT="$("${RUNTIME}" --rootfs=hostdir:"${VFS_ROOT}" \
	/mods/tests/t16_populate_read.wasm)"

echo "${OUT}"

echo "${OUT}" | grep -qE "t16_populate_read\.wasm: exit=0" \
	|| { echo "FAIL: populate extract or guest read failed" >&2; exit 1; }
echo "${OUT}" | grep -q "t16_populate_read: hello from populate" \
	|| { echo "FAIL: guest did not see populate_all() content under /scratch" >&2; exit 1; }

# --- Same again with lz4-compressed embed (util/lz4 block format). ---
"${ROOT}/scripts/lib/pack-image.sh" --lz4 "${FIXTURE}" "${EMBED}"
PM_METAL_FORCE_CMAKE=1 pm_linux_cmake_configure "${ROOT}/build/linux/runtime" \
	-DPM_METAL_POPULATE_EMBED="${EMBED}"
pm_linux_cmake_build "${ROOT}/build/linux/runtime" pm-linux-runtime

OUT_LZ4="$("${RUNTIME}" --rootfs=hostdir:"${VFS_ROOT}" \
	/mods/tests/t16_populate_read.wasm)"

echo "${OUT_LZ4}"

echo "${OUT_LZ4}" | grep -q "t16_populate_read: hello from populate" \
	|| { echo "FAIL: lz4-compressed populate embed did not extract correctly" >&2; exit 1; }

# --- Zip-slip: ustar entry name with ".." must not escape onto the host. ---
ZIPSLIP_DIR="$(mktemp -d)"
ZIPSLIP_EMBED="${ROOT}/build/linux/runtime/populate_zipslip_embed.c"
mkdir -p "${ZIPSLIP_DIR}/payload"
printf 'zipslip\n' > "${ZIPSLIP_DIR}/payload/evil.txt"
# Rewrite the member name to include ".." while packing.
tar --format=ustar -cf "${ZIPSLIP_DIR}/image.tar" \
	--transform='s|^payload/evil.txt$|scratch/../../evil.txt|' \
	-C "${ZIPSLIP_DIR}" payload/evil.txt
{
	echo "/* Generated zip-slip populate fixture — do not edit. */"
	echo "#include \"pymergetic/metal/mount/populate.h\""
	echo "#include <stddef.h>"
	echo "#include <stdint.h>"
	echo "static const uint8_t g_pm_metal_mount_populate_blob[] = {"
	od -An -v -tx1 "${ZIPSLIP_DIR}/image.tar" | sed 's/ \([0-9a-f][0-9a-f]\)/ 0x\1,/g'
	echo "};"
	echo "static void pm_metal_mount_populate_embed_register(void) __attribute__((constructor));"
	echo "static void pm_metal_mount_populate_embed_register(void)"
	echo "{"
	echo "	(void)pm_metal_mount_populate_register(g_pm_metal_mount_populate_blob,"
	echo "		sizeof(g_pm_metal_mount_populate_blob), (size_t)0u, 0u);"
	echo "}"
} > "${ZIPSLIP_EMBED}"
rm -rf "${ZIPSLIP_DIR}"

PM_METAL_FORCE_CMAKE=1 pm_linux_cmake_configure "${ROOT}/build/linux/runtime" \
	-DPM_METAL_POPULATE_EMBED="${ZIPSLIP_EMBED}"
pm_linux_cmake_build "${ROOT}/build/linux/runtime" pm-linux-runtime

VFS_ZIP="$(mktemp -d)"
mkdir -p "${VFS_ZIP}/etc"
cat > "${VFS_ZIP}/etc/fstab" <<EOF
scratch      /scratch    tmpfs      rw
EOF

OUT_ZIP="$("${RUNTIME}" --rootfs=hostdir:"${VFS_ZIP}" \
	/mods/tests/t16_populate_read.wasm)" || true
echo "${OUT_ZIP}"

if find "${VFS_ZIP}" -name 'evil.txt' | grep -q .; then
	echo "FAIL: zip-slip populate wrote evil.txt under vfs root" >&2
	rm -rf "${VFS_ZIP}"
	exit 1
fi
echo "${OUT_ZIP}" | grep -q "zipslip" \
	&& { echo "FAIL: zip-slip payload visible to guest" >&2; rm -rf "${VFS_ZIP}"; exit 1; }
rm -rf "${VFS_ZIP}"
rm -f "${ZIPSLIP_EMBED}"

# Rebuild without the embed so later verifies don't keep a stale fixture.
PM_METAL_FORCE_CMAKE=1 pm_linux_cmake_configure "${ROOT}/build/linux/runtime" \
	-DPM_METAL_POPULATE_EMBED=
pm_linux_cmake_build "${ROOT}/build/linux/runtime" pm-linux-runtime
rm -f "${EMBED}"

echo "verify-linux-populate: OK"
