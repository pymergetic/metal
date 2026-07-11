#!/usr/bin/env bash
# Zephyr native_sim runtime engine.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

NATIVE_BUILD="${ROOT}/build/zephyr/native_sim"

west build -p always -b native_sim/native/64 host/zephyr \
	--build-dir "${NATIVE_BUILD}" \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	-DDTC_OVERLAY_FILE="${PM_ZEPHYR_OVERLAY}" \
	-DEXTRA_CONF_FILE="${ROOT}/host/zephyr/boards/native_sim_native_64.conf"

echo "zephyr native_sim -> ${NATIVE_BUILD}/zephyr/zephyr.exe"

if [[ -f "${ROOT}/build/linux/engine/compile_commands.json" ]]; then
	if [[ -f "${ROOT}/build/zephyr/ide/compile_commands.json" ]]; then
		ZEPHYR_DB="${ROOT}/build/zephyr/ide/compile_commands.json" \
			"${ROOT}/scripts/merge-compile-commands.sh"
	elif [[ -f "${NATIVE_BUILD}/compile_commands.json" ]]; then
		ZEPHYR_DB="${NATIVE_BUILD}/compile_commands.json" \
			"${ROOT}/scripts/merge-compile-commands.sh"
	fi
fi
