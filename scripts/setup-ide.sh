#!/usr/bin/env bash
# Refresh compile_commands.json and seed autoconf.h for Cursor/clangd.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

source .venv/bin/activate 2>/dev/null || true
export ZEPHYR_BASE="${ZEPHYR_BASE:-${ROOT}/external/zephyr}"

BUILD_DIR="runtime/build/native_sim/native/64"
BUILD_DB="${BUILD_DIR}/compile_commands.json"
AUTOCONF="${BUILD_DIR}/zephyr/include/generated/zephyr/autoconf.h"
IDE_AUTOCONF="include/pymergetic/metal/ide/autoconf.h"

mkdir -p "$(dirname "$AUTOCONF")"
cp -f "$IDE_AUTOCONF" "$AUTOCONF"

if [[ ! -f "$BUILD_DB" ]]; then
  echo "Building native_sim to generate compile_commands.json ..."
  west build -b native_sim/native/64 runtime -d "$BUILD_DIR"
fi

ln -sf "$BUILD_DB" "${ROOT}/compile_commands.json"
echo "OK: ${ROOT}/compile_commands.json -> ${BUILD_DB}"
echo "OK: seeded ${AUTOCONF} (west build overwrites with generated config)"
echo ""
echo "Open workspace folder: ${ROOT}"
echo "Reload window: Developer: Reload Window"
