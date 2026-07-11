#!/usr/bin/env bash
# Zephyr runtime — native_sim + qemu_x86_64.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

"${ROOT}/scripts/verify-zephyr-native-sim.sh"
echo
"${ROOT}/scripts/verify-zephyr-qemu.sh"

echo
echo "zephyr verify: ok"
