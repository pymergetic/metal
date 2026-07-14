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
cp "${ROOT}/build/mods/t0_hello.wasm" "${ROOT}/build/mods/t1_read.wasm" "${ROOT}/build/mods/t3_shell_exec.wasm" \
	"${VFS_ROOT}/mods/"

OUT="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --vfs-root="${VFS_ROOT}" \
	/mods/t0_hello.wasm \
	/mods/t1_read.wasm \
	/mods/t3_shell_exec.wasm)"

echo "${OUT}"

echo "${OUT}" | grep -q "t0_hello" \
	|| { echo "FAIL: missing t0_hello output" >&2; exit 1; }
echo "${OUT}" | grep -q "hello from vfs root" \
	|| { echo "FAIL: missing t1_read output (vfs root not shared/1:1)" >&2; exit 1; }
echo "${OUT}" | grep -qE "t0_hello\.wasm: exit=0" \
	|| { echo "FAIL: t0_hello did not exit 0" >&2; exit 1; }
echo "${OUT}" | grep -qE "t1_read\.wasm: exit=0" \
	|| { echo "FAIL: t1_read did not exit 0" >&2; exit 1; }

# shell/guest_exec.h's native import works from scripted mode too now (not
# just console `load`+`run` — see docs/CONSOLE.md "Guest-callable commands"
# "Scope"): pm_metal_process_spawn()'s guest_out=stdout for this run mode
# (app.c) means the guest-exec-triggered "pwd" command's own output lands
# on the same real host stdout as t3_shell_exec's own printf, right here.
echo "${OUT}" | grep -q "t3_shell_exec: allowed=0 denied=-1001 not_found=-1000" \
	|| { echo "FAIL: guest-exec did not work from scripted mode" >&2; exit 1; }
echo "${OUT}" | grep -qE "t3_shell_exec\.wasm: exit=0" \
	|| { echo "FAIL: t3_shell_exec did not exit 0" >&2; exit 1; }

echo "verify-linux: OK"
