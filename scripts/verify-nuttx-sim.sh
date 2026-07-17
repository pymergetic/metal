#!/usr/bin/env bash
# NuttX sim smoke — same idea as verify-linux.sh's scripted tier, driven
# through nsh over the sim binary's stdin (hostfs mount + pm_metal).
# See src/nuttx/README.md.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NUTTX="${ROOT}/build/nuttx/sim/nuttx"
TIMEOUT_SECS="${NUTTX_VERIFY_TIMEOUT:-90}"

if [[ -f "${ROOT}/.venv/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${ROOT}/.venv/bin/activate"
fi

"${ROOT}/scripts/build-mod.sh"
"${ROOT}/scripts/build-nuttx-sim.sh"

if [[ ! -x "${NUTTX}" ]]; then
	echo "missing ${NUTTX}" >&2
	exit 1
fi

# Real file copies (not symlinks): NuttX hostfs readlink() on a host
# symlink returns EINVAL (22), which makes `ls` noisy. Load only needs
# open/read, but verify copies plain files like verify-linux.sh.
VFS_ROOT="$(mktemp -d)"
NSH_SCRIPT="$(mktemp)"
OUT="$(mktemp)"
trap 'rm -rf "${VFS_ROOT}" "${NSH_SCRIPT}" "${OUT}"' EXIT

printf 'hello from vfs root\n' >"${VFS_ROOT}/README"
# Short basenames keep each nsh line under CONFIG_LINE_MAX (80 on older
# builds; sim-metal.config raises it to 256 on rebuild).
cp "${ROOT}/build/mods/t0_hello.wasm" "${VFS_ROOT}/t0.wasm"
cp "${ROOT}/build/mods/t1_read.wasm" "${VFS_ROOT}/t1.wasm"
cp "${ROOT}/build/mods/t3_util_native.wasm" "${VFS_ROOT}/t3.wasm"
cp "${ROOT}/build/mods/t4_getpid.wasm" "${VFS_ROOT}/t4.wasm"

# One mod per pm_metal invocation — each line stays ≤80 chars even on a
# stale binary that still has CONFIG_LINE_MAX=80.
PM="pm_metal --memory=8388608 --bytecode-memory=524288 --vfs-root=/v"
cat >"${NSH_SCRIPT}" <<EOF
mount -t hostfs -o fs=${VFS_ROOT} /v
${PM} /t0.wasm
${PM} /t1.wasm
${PM} /t3.wasm
${PM} /t4.wasm
poweroff
EOF

echo "=== nsh script ==="
cat "${NSH_SCRIPT}"
echo "=== run: nuttx sim (timeout ${TIMEOUT_SECS}s) ==="

set +e
timeout "${TIMEOUT_SECS}" "${NUTTX}" <"${NSH_SCRIPT}" >"${OUT}" 2>&1
rc=$?
set -e
cat "${OUT}"

if [[ "${rc}" -eq 124 ]]; then
	echo "FAIL: nuttx sim timed out after ${TIMEOUT_SECS}s" >&2
	exit 1
fi

grep -qF "t0_hello" "${OUT}" \
	|| { echo "FAIL: missing t0_hello output" >&2; exit 1; }
grep -qE "t0\.wasm: exit=0" "${OUT}" \
	|| { echo "FAIL: t0 did not exit 0" >&2; exit 1; }
grep -qF "hello from vfs root" "${OUT}" \
	|| { echo "FAIL: missing t1_read output (vfs root not shared/1:1)" >&2; exit 1; }
grep -qE "t1\.wasm: exit=0" "${OUT}" \
	|| { echo "FAIL: t1 did not exit 0" >&2; exit 1; }
grep -qF "t3_util_native: size=88 MiB" "${OUT}" \
	|| { echo "FAIL: size.h import wrong/missing" >&2; exit 1; }
grep -qE "t3\.wasm: exit=0" "${OUT}" \
	|| { echo "FAIL: t3 did not exit 0" >&2; exit 1; }
grep -qE "t4\.wasm: exit=0" "${OUT}" \
	|| { echo "FAIL: t4 did not exit 0" >&2; exit 1; }

echo "verify-nuttx-sim: ok"
