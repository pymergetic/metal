#!/usr/bin/env bash
# Point clangd at merged compile_commands (linux + zephyr native_sim w/ wasm mods).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

if [[ ! -f "${ROOT}/build/linux/engine/compile_commands.json" ]]; then
	"${ROOT}/scripts/build-linux.sh"
fi

if [[ -f "${ROOT}/.venv/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${ROOT}/.venv/bin/activate"
fi
export ZEPHYR_BASE="${ROOT}/external/zephyr"
export WAMR_ROOT_DIR="${ROOT}/external/wamr"

ZEPHYR_BUILD="${ROOT}/build/zephyr/ide"
OVERLAY="${ROOT}/host/zephyr/boards/pm_ramdisk.overlay"

west build -p always -b native_sim/native/64 host/zephyr \
	--build-dir "${ZEPHYR_BUILD}" \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	-DDTC_OVERLAY_FILE="${OVERLAY}" \
	-DEXTRA_CONF_FILE="${ROOT}/host/zephyr/boards/native_sim_native_64.conf"

ZEPHYR_DB="${ZEPHYR_BUILD}/compile_commands.json" \
	LINUX_DB="${ROOT}/build/linux/engine/compile_commands.json" \
	"${ROOT}/scripts/merge-compile-commands.sh"
