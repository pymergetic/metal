#!/usr/bin/env bash
# Linux default suite — run/collect here; expect markers via verify.d/suite.sh.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"
PORT="${ROOT}/scripts/verify.d/port/linux"

"${ROOT}/scripts/build.d/port/linux/default.sh"

RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}"' EXIT

printf 'hello from vfs root\n' > "${VFS_ROOT}/README"
pm_suite_stage_package "${VFS_ROOT}"

OUT="$("${RUNTIME}" --vfs-root="${VFS_ROOT}" \
	/mods/tests/t0_hello.wasm \
	/mods/tests/t1_read.wasm \
	/mods/tests/t3_util_native.wasm \
	/mods/tests/t4_getpid.wasm \
	/mods/tests/t9_multimod_app.wasm \
	/mods/tests/t23_pthread.wasm \
	/mods/tests/t31_net_util.wasm)"

echo "${OUT}"
pm_suite_expect_scripted
echo "verify-linux: scripted OK"

"${PORT}/process.sh"
"${PORT}/tmpfs.sh"
"${PORT}/populate.sh"
"${PORT}/proc.sh"
"${PORT}/python.sh" linux

echo "verify-linux: OK"
