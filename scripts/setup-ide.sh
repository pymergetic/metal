#!/usr/bin/env bash
# Generate compile_commands.json and .clangd (linux scaffold) for this checkout.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/build/linux/runtime"

cmake -S "${ROOT}" -B "${BUILD}"
ln -sf "${BUILD}/compile_commands.json" "${ROOT}/compile_commands.json"
echo "compile_commands.json -> ${BUILD}/compile_commands.json"

# .clangd needs a few absolute paths (see .clangd.template's own comment for
# why relative ones don't work there) but the absolute value is specific to
# this checkout's own location on disk — .clangd.template is the tracked,
# portable source of truth (placeholder @@ROOT@@), and this generated .clangd
# is gitignored so no machine-specific path ever lands in version control.
sed "s|@@ROOT@@|${ROOT}|g" "${ROOT}/.clangd.template" > "${ROOT}/.clangd"
echo ".clangd -> generated from .clangd.template (ROOT=${ROOT})"
