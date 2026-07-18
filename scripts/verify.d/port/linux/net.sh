#!/usr/bin/env bash
# Smoke util/crypto + net/{dns,ntp,http} via mods/tests/t31_net_util.wasm (needs network).
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"
RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
VFS="${ROOT}/build/verify-linux-net-vfs"

if [ ! -x "${RUNTIME}" ]; then
	"${ROOT}/scripts/build.d/port/linux/default.sh"
fi

rm -rf "${VFS}"

OUT="$("${RUNTIME}" --vfs-root="${VFS}" \
	/mods/tests/t31_net_util.wasm 2>&1)" || {
	echo "${OUT}"
	echo "FAIL: runtime exited non-zero" >&2
	exit 1
}
echo "${OUT}"

echo "${OUT}" | grep -qF "t31_net_util: hash ok" \
	|| { echo "FAIL: hash" >&2; exit 1; }
echo "${OUT}" | grep -qF "t31_net_util: aead round-trip ok" \
	|| { echo "FAIL: aead" >&2; exit 1; }
echo "${OUT}" | grep -qE "t31_net_util: net dns n=[0-9]+" \
	|| { echo "FAIL: net dns" >&2; exit 1; }
echo "${OUT}" | grep -qE "t31_net_util: ntp unix=[0-9]+" \
	|| { echo "FAIL: ntp" >&2; exit 1; }
echo "${OUT}" | grep -qF "t31_net_util: net http ok" \
	|| { echo "FAIL: net http" >&2; exit 1; }

echo "verify-linux-net: OK"
