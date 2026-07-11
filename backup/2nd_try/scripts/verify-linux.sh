#!/usr/bin/env bash
# Linux runtime: shared entry + linux lower port.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LINUX_ENGINE="${ROOT}/build/linux/engine/pm-linux-engine"
VFS_LINUX="${ROOT}/build/linux/vfs"
OUT_LINUX="$(mktemp)"

cleanup() { rm -f "${OUT_LINUX}"; }
trap cleanup EXIT

"${ROOT}/scripts/build-linux.sh"

rm -rf "${VFS_LINUX}"
mkdir -p "${VFS_LINUX}"
"${LINUX_ENGINE}" "${VFS_LINUX}" >"${OUT_LINUX}" 2>&1

echo "=== linux runtime ==="
cat "${OUT_LINUX}"
echo

grep -q "runtime: target=linux" "${OUT_LINUX}"
grep -q "pymergetic orchestrator" "${OUT_LINUX}"
grep -q "machine_ram = 536870912" "${OUT_LINUX}"
grep -q "ready" "${OUT_LINUX}"

echo "linux verify: ok"
