#!/usr/bin/env bash
# Zephyr qemu_x86_64 runtime engine.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

QEMU_BUILD="${ROOT}/build/zephyr/qemu_x86_64"

# Default: incremental. PM_ZEPHYR_PRISTINE=1 for a clean tree (CI / weird cmake).
# Avoid empty-array expansion before `west build` — it can break `\` continuations
# and leave `--build-dir` as a bare command.
if [[ "${PM_ZEPHYR_PRISTINE:-0}" == "1" ]]; then
	west build -p always -b qemu_x86_64 src/zephyr \
		--build-dir "${QEMU_BUILD}" \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DDTC_OVERLAY_FILE="${PM_ZEPHYR_OVERLAY}" \
		-DEXTRA_CONF_FILE="${ROOT}/src/zephyr/boards/qemu_x86_64.conf"
else
	west build -b qemu_x86_64 src/zephyr \
		--build-dir "${QEMU_BUILD}" \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DDTC_OVERLAY_FILE="${PM_ZEPHYR_OVERLAY}" \
		-DEXTRA_CONF_FILE="${ROOT}/src/zephyr/boards/qemu_x86_64.conf"
fi

echo "zephyr qemu_x86_64 -> ${QEMU_BUILD}/zephyr/zephyr.elf"
