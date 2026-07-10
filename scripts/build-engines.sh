#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build/slice/engine"

cmake -S "${ROOT}/host/linux" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD}" -j

ln -sf "${BUILD}/compile_commands.json" "${ROOT}/compile_commands.json"

echo "linux engine  -> ${BUILD}/pm-linux-engine"
echo "zephyr engine -> ${BUILD}/pm-zephyr-engine"
