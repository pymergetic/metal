#!/usr/bin/env bash
# Configure + build NuttX sim with Metal (pm_metal) via NuttX CMake.
# Prerequisites: scripts/setup.d/port/nuttx/default.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
NUTTX_DIR="${NUTTX_DIR:-${ROOT}/external/nuttx}"
APPS_DIR="${APPS_DIR:-${ROOT}/external/nuttx-apps}"
BUILD_DIR="${ROOT}/build/nuttx/sim"
FRAGMENT="${ROOT}/src/nuttx/configs/sim-metal.config"

# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/nuttx-cmake.sh"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/guest-pkgs.sh"

pm_nuttx_build_prereqs
if ! command -v genromfs >/dev/null 2>&1; then
	echo "genromfs missing — NuttX sim needs it for etc romfs (e.g. sudo apt install genromfs)" >&2
	exit 1
fi

pm_guest_pkgs_compose
pm_nuttx_cmake_build "${BUILD_DIR}" "sim:nsh" "${FRAGMENT}"

echo "nuttx sim build -> ${BUILD_DIR}"
echo "  run: ${BUILD_DIR}/nuttx (then nsh: pm_metal --memory=...)"
