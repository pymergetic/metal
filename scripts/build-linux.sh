#!/usr/bin/env bash
# Configure + build pm-linux-runtime (WAMR wired in via src/linux/CMakeLists.txt).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/build/linux/runtime"
JOBS="$(nproc 2>/dev/null || echo 4)"

cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
cmake --build "${BUILD}" --target pm-linux-runtime -j"${JOBS}"

echo "pm-linux-runtime -> ${BUILD}/pm-linux-runtime"
