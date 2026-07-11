#!/usr/bin/env bash
# Generate compile_commands.json for clangd (linux scaffold).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/build/linux/runtime"

cmake -S "${ROOT}" -B "${BUILD}"
ln -sf "${BUILD}/compile_commands.json" "${ROOT}/compile_commands.json"
echo "compile_commands.json -> ${BUILD}/compile_commands.json"
