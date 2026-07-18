#!/usr/bin/env bash
# Configure + build pm-linux-runtime (WAMR wired in via src/linux/CMakeLists.txt).
# Composes named lz4 guest packages first so pkgs.cmake is linked in.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/linux-cmake.sh"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/guest-pkgs.sh"
BUILD="${ROOT}/build/linux/runtime"

pm_guest_pkgs_compose

# Reconfigure when package list/embeds change (pkgs.cmake is the cmake input).
if [[ ! -f "${BUILD}/CMakeCache.txt" \
	|| "${PM_GUEST_PKGS}/pkgs.cmake" -nt "${BUILD}/CMakeCache.txt" ]]; then
	PM_METAL_FORCE_CMAKE=1
fi

PM_METAL_FORCE_CMAKE="${PM_METAL_FORCE_CMAKE:-0}" pm_linux_cmake_configure "${BUILD}"
pm_linux_cmake_build "${BUILD}" pm-linux-runtime

echo "pm-linux-runtime -> ${BUILD}/pm-linux-runtime"
