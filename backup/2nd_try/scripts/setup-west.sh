#!/usr/bin/env bash
# Fetch Zephyr + WAMR west deps into external/ (gitignored).
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

# WAMR's zephyr/module.yml sets kconfig-ext without shipping Kconfig — fix module.yml.
WAMR_MODULE_YML="${WAMR_ROOT}/zephyr/module.yml"
if [[ -f "${WAMR_MODULE_YML}" ]]; then
	cat >"${WAMR_MODULE_YML}" <<'EOF'
name: wasm-micro-runtime

build:
  cmake-ext: True
  kconfig: Kconfig
EOF
fi

if [[ ! -f "${WAMR_ROOT}/Kconfig" ]]; then
	cat >"${WAMR_ROOT}/Kconfig" <<'EOF'
# WAMR is wired from host/zephyr/CMakeLists.txt (runtime_lib.cmake).
EOF
fi

# WAMR 2.4.x: pthread condattr + Zephyr WASI locking
WAMR_SSP_CONFIG="${WAMR_ROOT}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/ssp_config.h"
if [[ -f "${WAMR_SSP_CONFIG}" ]] && ! grep -q 'BH_PLATFORM_ZEPHYR' "${WAMR_SSP_CONFIG}"; then
	sed -i \
		-e 's/#if !defined(__APPLE__) && !defined(BH_PLATFORM_LINUX_SGX) && !defined(_WIN32) \\$/#if !defined(__APPLE__) \&\& !defined(BH_PLATFORM_LINUX_SGX) \&\& !defined(_WIN32) \\\n    \&\& !defined(BH_PLATFORM_ZEPHYR) \\/' \
		"${WAMR_SSP_CONFIG}"
fi
WAMR_PLATFORM_H="${WAMR_ROOT}/core/shared/platform/zephyr/platform_internal.h"
if [[ -f "${WAMR_PLATFORM_H}" ]] && grep -q '#define mutex_init(mtx)' "${WAMR_PLATFORM_H}"; then
	sed -i \
		-e 's/#define mutex_t /#define zmutex_t /g' \
		-e 's/#define mutex_init/#define zmutex_init/g' \
		-e 's/#define mutex_lock/#define zmutex_lock/g' \
		-e 's/#define mutex_unlock/#define zmutex_unlock/g' \
		-e 's/typedef mutex_t korp_mutex/typedef zmutex_t korp_mutex/g' \
		-e 's/    mutex_t wait_list_lock;/    zmutex_t wait_list_lock;/g' \
		"${WAMR_PLATFORM_H}"
fi

# zephyr_thread.c still references mutex_t after zmutex rename
WAMR_THREAD_C="${WAMR_ROOT}/core/shared/platform/zephyr/zephyr_thread.c"
if [[ -f "${WAMR_THREAD_C}" ]] && grep -q 'static mutex_t ' "${WAMR_THREAD_C}"; then
	sed -i \
		-e 's/mutex_t/zmutex_t/g' \
		-e 's/mutex_lock(/zmutex_lock(/g' \
		-e 's/mutex_unlock(/zmutex_unlock(/g' \
		-e 's/mutex_init(/zmutex_init(/g' \
		"${WAMR_THREAD_C}"
fi

echo "west ready"
echo "  ZEPHYR_BASE=${ZEPHYR_BASE}"
echo "  WAMR_ROOT_DIR=${WAMR_ROOT}"
