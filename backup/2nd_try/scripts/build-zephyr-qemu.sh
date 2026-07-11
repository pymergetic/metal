#!/usr/bin/env bash
# Zephyr qemu_x86_64 runtime engine.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

QEMU_BUILD="${ROOT}/build/zephyr/qemu_x86_64"

west build -p always -b qemu_x86_64 host/zephyr \
	--build-dir "${QEMU_BUILD}" \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	-DDTC_OVERLAY_FILE="${PM_ZEPHYR_OVERLAY}" \
	-DEXTRA_CONF_FILE="${ROOT}/host/zephyr/boards/qemu_x86_64.conf"

echo "zephyr qemu_x86_64 -> ${QEMU_BUILD}/zephyr/zephyr.elf"
