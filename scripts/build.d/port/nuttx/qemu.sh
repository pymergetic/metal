#!/usr/bin/env bash
# Configure + build NuttX qemu-intel64 with Metal (pm_metal) via NuttX CMake.
# Prerequisites: scripts/setup.d/port/nuttx/default.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
NUTTX_DIR="${NUTTX_DIR:-${ROOT}/external/nuttx}"
APPS_DIR="${APPS_DIR:-${ROOT}/external/nuttx-apps}"
BUILD_DIR="${ROOT}/build/nuttx/qemu"
FRAGMENT="${ROOT}/src/nuttx/configs/qemu-metal.config"

# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/nuttx-cmake.sh"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/guest-pkgs.sh"

pm_nuttx_build_prereqs
if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "qemu-system-x86_64 missing — install qemu-system-x86" >&2
	exit 1
fi

pm_guest_pkgs_compose
pm_nuttx_cmake_build "${BUILD_DIR}" "qemu-intel64:nsh" "${FRAGMENT}"

if [[ ! -f "${BUILD_DIR}/nuttx" ]]; then
	echo "missing kernel image ${BUILD_DIR}/nuttx" >&2
	exit 1
fi

echo "nuttx qemu build -> ${BUILD_DIR}"
echo "  kernel: ${BUILD_DIR}/nuttx"
echo "  run: scripts/verify nuttx qemu"
