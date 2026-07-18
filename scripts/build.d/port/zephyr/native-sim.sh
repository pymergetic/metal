#!/usr/bin/env bash
# Zephyr native_sim runtime engine.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"

# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/zephyr-env.sh"

NATIVE_BUILD="${ROOT}/build/zephyr/native_sim"

# Default: incremental. PM_ZEPHYR_PRISTINE=1 for a clean tree (CI / weird cmake).
# Avoid empty-array expansion before `west build` — it can break `\` continuations
# and leave `--build-dir` as a bare command.
if [[ "${PM_ZEPHYR_PRISTINE:-0}" == "1" ]]; then
	west build -p always -b native_sim/native/64 src/zephyr \
		--build-dir "${NATIVE_BUILD}" \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DDTC_OVERLAY_FILE="${PM_ZEPHYR_OVERLAY}" \
		-DEXTRA_CONF_FILE="${ROOT}/src/zephyr/boards/native_sim_native_64.conf"
else
	west build -b native_sim/native/64 src/zephyr \
		--build-dir "${NATIVE_BUILD}" \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DDTC_OVERLAY_FILE="${PM_ZEPHYR_OVERLAY}" \
		-DEXTRA_CONF_FILE="${ROOT}/src/zephyr/boards/native_sim_native_64.conf"
fi

echo "zephyr native_sim -> ${NATIVE_BUILD}/zephyr/zephyr.exe"
