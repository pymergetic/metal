#!/usr/bin/env bash
# init -> load -> run -> unload -> shutdown, on linux, using the dynamic
# loader API — two mods, one process, one shared vfs_root.
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
trap 'rm -rf "${VFS_ROOT}"' EXIT

printf 'hello from vfs root\n' > "${VFS_ROOT}/README"

# .wasm files are loaded from inside the VFS root, same as any other guest
# path — the loader resolves them against --vfs-root, never a host path
# outside the tree, so this works unchanged when vfs_root is a mounted
# ramdisk on zephyr instead of a host dir.
mkdir -p "${VFS_ROOT}/mods"
cp "${ROOT}/build/mods/t0_hello.wasm" "${ROOT}/build/mods/t1_read.wasm" "${VFS_ROOT}/mods/"

OUT="$("${RUNTIME}" --memory=16777216 --vfs-root="${VFS_ROOT}" \
	/mods/t0_hello.wasm \
	/mods/t1_read.wasm)"

echo "${OUT}"

echo "${OUT}" | grep -q "t0_hello" \
	|| { echo "FAIL: missing t0_hello output" >&2; exit 1; }
echo "${OUT}" | grep -q "hello from vfs root" \
	|| { echo "FAIL: missing t1_read output (vfs root not shared/1:1)" >&2; exit 1; }
echo "${OUT}" | grep -qE "t0_hello\.wasm: exit=0" \
	|| { echo "FAIL: t0_hello did not exit 0" >&2; exit 1; }
echo "${OUT}" | grep -qE "t1_read\.wasm: exit=0" \
	|| { echo "FAIL: t1_read did not exit 0" >&2; exit 1; }

echo "verify-linux: OK"
