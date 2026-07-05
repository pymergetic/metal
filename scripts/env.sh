#!/usr/bin/env bash
# Source:  source scripts/env.sh

: "${METAL_ROOT:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

ZEPHYR_BASE="${METAL_ROOT}/third_party/zephyr"
ZEPHYR_BUILD="${METAL_ROOT}/build/zephyr"
METAL_APP="${METAL_ROOT}/src/pymergetic/metal/app"
METAL_MODULE_ROOT="${METAL_ROOT}/src/pymergetic/metal"

if [[ -x "${METAL_ROOT}/.venv/bin/west" ]]; then
	PATH="${METAL_ROOT}/.venv/bin:${PATH}"
fi

if [[ ! -f "${ZEPHYR_BASE}/zephyr-env.sh" ]]; then
	echo "Missing Zephyr tree: ${ZEPHYR_BASE}" >&2
	echo "Run: git submodule update --init third_party/zephyr" >&2
	return 1 2>/dev/null || exit 1
fi

if [[ ! -d "${METAL_ROOT}/.west" ]]; then
	echo "West workspace not initialized in ${METAL_ROOT}" >&2
	echo "Run: ./scripts/setup-west" >&2
	return 1 2>/dev/null || exit 1
fi

export METAL_ROOT METAL_APP METAL_MODULE_ROOT ZEPHYR_BASE ZEPHYR_BUILD

# shellcheck source=/dev/null
source "${ZEPHYR_BASE}/zephyr-env.sh"
