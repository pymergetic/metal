#!/usr/bin/env bash
# Zephyr native_sim runtime with WAMR + mod-smoke verify.
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "$0")" && pwd)/zephyr-env.sh"

NATIVE_MOD_BUILD="${ROOT}/build/zephyr/native_sim_mod"
MOD_CONF="${ROOT}/host/zephyr/boards/native_sim_native_64.conf;${ROOT}/host/zephyr/boards/verify_mod.conf"

"${ROOT}/scripts/gen-mod-smoke-embed.sh"

west build -p always -b native_sim/native/64 host/zephyr \
	--build-dir "${NATIVE_MOD_BUILD}" \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	-DDTC_OVERLAY_FILE="${PM_ZEPHYR_OVERLAY}" \
	-DEXTRA_CONF_FILE="${MOD_CONF}"

echo "zephyr native_sim (mod) -> ${NATIVE_MOD_BUILD}/zephyr/zephyr.exe"
