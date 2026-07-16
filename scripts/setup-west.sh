#!/usr/bin/env bash
# Fetch Zephyr + WAMR west deps into external/ (gitignored), then apply
# this tree's tracked patches/wamr/*.patch so external/wamr stays a plain
# upstream checkout plus reproducible patches — never hand-edit or sed
# the checkout in place. See docs/SOURCETREE.md "Vendoring".
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

if [[ -f "${ROOT}/.venv/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${ROOT}/.venv/bin/activate"
fi

if ! command -v west >/dev/null 2>&1; then
	echo "west not found — create .venv and pip install west" >&2
	exit 1
fi

west update

export ZEPHYR_BASE="${ROOT}/external/zephyr"
if [[ ! -f "${ZEPHYR_BASE}/share/zephyr-package/cmake/ZephyrConfig.cmake" ]]; then
	echo "Zephyr not found at ${ZEPHYR_BASE} after west update" >&2
	exit 1
fi

WAMR_ROOT="${ROOT}/external/wamr"
if [[ ! -f "${WAMR_ROOT}/build-scripts/runtime_lib.cmake" ]]; then
	echo "WAMR not found at ${WAMR_ROOT} after west update" >&2
	exit 1
fi

# Re-pin + patch WAMR the same way as a plain setup-wamr.sh run so west
# checkouts and standalone clones stay identical.
"${ROOT}/scripts/setup-wamr.sh"

echo "west ready"
echo "  ZEPHYR_BASE=${ZEPHYR_BASE}"
echo "  WAMR_ROOT_DIR=${WAMR_ROOT}"
