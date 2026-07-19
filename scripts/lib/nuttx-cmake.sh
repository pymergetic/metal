# Shared NuttX CMake helpers for Metal board builds (sim / qemu).
# shellcheck shell=bash

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

# Prerequisites shared by sim/qemu builds. Sets nothing; exits on failure.
pm_nuttx_build_prereqs() {
	if [[ -f "${ROOT}/.venv/bin/activate" ]]; then
		# shellcheck disable=SC1091
		source "${ROOT}/.venv/bin/activate"
	fi
	if ! python3 -c "import kconfiglib" 2>/dev/null; then
		echo "kconfiglib missing — run: pip install kconfiglib (or ./scripts/setup.d/port/nuttx/default.sh)" >&2
		return 1
	fi
	if [[ ! -f "${NUTTX_DIR}/CMakeLists.txt" ]]; then
		echo "NuttX not found at ${NUTTX_DIR}; run scripts/setup.d/port/nuttx/default.sh" >&2
		return 1
	fi
	if [[ ! -L "${APPS_DIR}/system/pm_metal" && ! -d "${APPS_DIR}/system/pm_metal" ]]; then
		echo "Metal app not registered at ${APPS_DIR}/system/pm_metal; run scripts/setup.d/port/nuttx/default.sh" >&2
		return 1
	fi
	if [[ ! -f "${ROOT}/external/wamr/build-scripts/runtime_lib.cmake" ]]; then
		echo "WAMR not found; run scripts/setup.d/deps/wamr.sh / setup nuttx" >&2
		return 1
	fi
	return 0
}

# Configure + merge fragment + build.
# Args: <build_dir> <board_config> <fragment_path>
pm_nuttx_cmake_build() {
	local build_dir=$1
	local board_config=$2
	local fragment=$3

	mkdir -p "${build_dir}"
	export WAMR_ROOT_DIR="${ROOT}/external/wamr"

	cmake -S "${NUTTX_DIR}" -B "${build_dir}" \
		-DBOARD_CONFIG="${board_config}" \
		-DNUTTX_APPS_DIR="${APPS_DIR}" \
		-DCMAKE_BUILD_TYPE=Release

	if [[ -f "${build_dir}/.config" ]]; then
		pm_nuttx_merge_fragment "${build_dir}/.config" "${fragment}"
		# Fill defaults for symbols that became visible. olddefconfig can drop
		# apps symbols if APPSDIR isn't visible — re-merge.
		cmake --build "${build_dir}" --target olddefconfig
		pm_nuttx_merge_fragment "${build_dir}/.config" "${fragment}"
		cmake -S "${NUTTX_DIR}" -B "${build_dir}" \
			-DNUTTX_APPS_DIR="${APPS_DIR}"
	fi

	cmake --build "${build_dir}" -j"$(nproc)"
}
