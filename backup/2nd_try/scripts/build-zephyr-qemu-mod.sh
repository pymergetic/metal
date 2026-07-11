#!/usr/bin/env bash
# Zephyr qemu_x86_64 runtime with WAMR + mod-smoke verify.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

QEMU_MOD_BUILD="${ROOT}/build/zephyr/qemu_x86_64_mod"
MOD_CONF="${ROOT}/host/zephyr/boards/qemu_x86_64.conf;${ROOT}/host/zephyr/boards/verify_mod.conf"

"${ROOT}/scripts/gen-mod-smoke-embed.sh"

west build -p always -b qemu_x86_64 host/zephyr \
	--build-dir "${QEMU_MOD_BUILD}" \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	-DDTC_OVERLAY_FILE="${PM_ZEPHYR_OVERLAY}" \
	-DEXTRA_CONF_FILE="${MOD_CONF}"

echo "zephyr qemu_x86_64 (mod) -> ${QEMU_MOD_BUILD}/zephyr/zephyr.elf"
