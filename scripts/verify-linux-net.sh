#!/usr/bin/env bash
# Smoke util/{crypto,ntp,http} via mods/t31_net_util.wasm (needs network).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
MOD="${ROOT}/build/mods/t31_net_util.wasm"
VFS="${ROOT}/build/verify-linux-net-vfs"

if [ ! -x "${RUNTIME}" ]; then
	"${ROOT}/scripts/build-linux.sh"
fi
"${ROOT}/scripts/build-mod.sh"

rm -rf "${VFS}"
mkdir -p "${VFS}/mods"
cp "${MOD}" "${VFS}/mods/t31_net_util.wasm"

OUT="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --vfs-root="${VFS}" \
	/mods/t31_net_util.wasm 2>&1)" || {
	echo "${OUT}"
	echo "FAIL: runtime exited non-zero" >&2
	exit 1
}
echo "${OUT}"

echo "${OUT}" | grep -qF "t31_net_util: hash ok" \
	|| { echo "FAIL: hash" >&2; exit 1; }
echo "${OUT}" | grep -qF "t31_net_util: aead round-trip ok" \
	|| { echo "FAIL: aead" >&2; exit 1; }
echo "${OUT}" | grep -qE "t31_net_util: ntp unix=[0-9]+" \
	|| { echo "FAIL: ntp" >&2; exit 1; }
echo "${OUT}" | grep -qF "t31_net_util: http ok" \
	|| { echo "FAIL: http" >&2; exit 1; }

echo "verify-linux-net: OK"
