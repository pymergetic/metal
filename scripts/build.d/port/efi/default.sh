#!/usr/bin/env bash
# Freestanding EFI Metal — scaffold build (object only until PE/COFF link lands).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
BUILD_DIR="${ROOT}/build/efi"

mkdir -p "${BUILD_DIR}"
cmake -S "${ROOT}/src/efi" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}"

echo "efi scaffold build -> ${BUILD_DIR}"
echo "  next: PE/COFF link to metal.efi + QEMU/OVMF (see src/efi/README.md)"
