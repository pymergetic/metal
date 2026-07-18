#!/usr/bin/env bash
# Configure + build NuttX sim with Metal (pm_metal) via NuttX CMake.
# Prerequisites: scripts/setup.d/port/nuttx/default.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
NUTTX_DIR="${NUTTX_DIR:-${ROOT}/external/nuttx}"
APPS_DIR="${APPS_DIR:-${ROOT}/external/nuttx-apps}"
BUILD_DIR="${ROOT}/build/nuttx/sim"
FRAGMENT="${ROOT}/src/nuttx/configs/sim-metal.config"

if [[ -f "${ROOT}/.venv/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${ROOT}/.venv/bin/activate"
fi
if ! python3 -c "import kconfiglib" 2>/dev/null; then
	echo "kconfiglib missing — run: pip install kconfiglib (or ./scripts/setup.d/port/nuttx/default.sh)" >&2
	exit 1
fi
if ! command -v genromfs >/dev/null 2>&1; then
	echo "genromfs missing — NuttX sim needs it for etc romfs (e.g. sudo apt install genromfs)" >&2
	exit 1
fi

if [[ ! -f "${NUTTX_DIR}/CMakeLists.txt" ]]; then
	echo "NuttX not found at ${NUTTX_DIR}; run scripts/setup.d/port/nuttx/default.sh" >&2
	exit 1
fi
if [[ ! -L "${APPS_DIR}/system/pm_metal" && ! -d "${APPS_DIR}/system/pm_metal" ]]; then
	echo "Metal app not registered at ${APPS_DIR}/system/pm_metal; run scripts/setup.d/port/nuttx/default.sh" >&2
	exit 1
fi
if [[ ! -f "${ROOT}/external/wamr/build-scripts/runtime_lib.cmake" ]]; then
	echo "WAMR not found; run scripts/setup.d/deps/wamr.sh / setup-nuttx.sh" >&2
	exit 1
fi

# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/guest-pkgs.sh"
pm_guest_pkgs_compose

mkdir -p "${BUILD_DIR}"

export WAMR_ROOT_DIR="${ROOT}/external/wamr"

# Board config then merge Metal fragment (kconfiglib / cmake configure).
cmake -S "${NUTTX_DIR}" -B "${BUILD_DIR}" \
	-DBOARD_CONFIG=sim:nsh \
	-DNUTTX_APPS_DIR="${APPS_DIR}" \
	-DCMAKE_BUILD_TYPE=Release

# Merge fragment into .config: KEY=val sets; "# KEY is not set" unsets.
pm_nuttx_merge_fragment() {
	local cfg="$1"
	local frag="$2"
	while IFS= read -r line || [[ -n "${line}" ]]; do
		[[ -z "${line}" ]] && continue
		if [[ "${line}" =~ ^#\ (CONFIG_[A-Za-z0-9_]+)\ is\ not\ set$ ]]; then
			local key="${BASH_REMATCH[1]}"
			if grep -q "^${key}=" "${cfg}" 2>/dev/null; then
				sed -i "s|^${key}=.*|# ${key} is not set|" "${cfg}"
			elif ! grep -q "^# ${key} is not set\$" "${cfg}" 2>/dev/null; then
				echo "# ${key} is not set" >>"${cfg}"
			fi
			continue
		fi
		[[ "${line}" =~ ^# ]] && continue
		local key="${line%%=*}"
		if grep -q "^${key}=" "${cfg}" 2>/dev/null; then
			sed -i "s|^${key}=.*|${line}|" "${cfg}"
		elif grep -q "^# ${key} is not set\$" "${cfg}" 2>/dev/null; then
			sed -i "s|^# ${key} is not set\$|${line}|" "${cfg}"
		else
			echo "${line}" >>"${cfg}"
		fi
	done <"${frag}"
}

if [[ -f "${BUILD_DIR}/.config" ]]; then
	pm_nuttx_merge_fragment "${BUILD_DIR}/.config" "${FRAGMENT}"
	# Fill defaults for symbols that became visible (IOB_*, NET_* callbacks).
	# olddefconfig can drop apps symbols if APPSDIR isn't visible — re-merge.
	cmake --build "${BUILD_DIR}" --target olddefconfig
	pm_nuttx_merge_fragment "${BUILD_DIR}/.config" "${FRAGMENT}"
	cmake -S "${NUTTX_DIR}" -B "${BUILD_DIR}" \
		-DNUTTX_APPS_DIR="${APPS_DIR}"
fi

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "nuttx sim build -> ${BUILD_DIR}"
echo "  run: ${BUILD_DIR}/nuttx (then nsh: pm_metal --memory=...)"
