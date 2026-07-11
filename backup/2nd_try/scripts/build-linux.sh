#!/usr/bin/env bash
# Linux runtime engine.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build/linux/engine"

cmake -S "${ROOT}/host/linux" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "${BUILD}" -j

if [[ -f "${ROOT}/build/zephyr/native_sim/compile_commands.json" ]]; then
	"${ROOT}/scripts/merge-compile-commands.sh"
else
	ln -sf "${BUILD}/compile_commands.json" "${ROOT}/compile_commands.json"
fi

echo "linux engine -> ${BUILD}/pm-linux-engine"
