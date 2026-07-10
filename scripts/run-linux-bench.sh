#!/usr/bin/env bash
# Build and run host glibc allocator bench for comparison with Zephyr TLSF blob.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/runtime/linux/bench/build"

cmake -S "${ROOT}/runtime/linux/bench" -B "${BUILD}"
cmake --build "${BUILD}" -j "$(nproc)"
echo
"${BUILD}/pm-linux-bench"
