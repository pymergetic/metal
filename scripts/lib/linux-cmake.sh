# Quiet-ish Linux cmake configure + build helpers.
# Reconfigure only when cache is missing or PM_METAL_FORCE_CMAKE=1.
# shellcheck shell=bash

pm_linux_cmake_configure() {
	# pm_linux_cmake_configure <build-dir> [extra cmake args...]
	local build="$1"
	shift
	mkdir -p "${build}"
	if [[ ! -f "${build}/CMakeCache.txt" || "${PM_METAL_FORCE_CMAKE:-0}" == "1" ]]; then
		cmake -S "${ROOT}" -B "${build}" \
			--log-level=NOTICE \
			-DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
			"$@"
	fi
}

pm_linux_cmake_build() {
	# pm_linux_cmake_build <build-dir> <target>
	local build="$1" target="$2"
	local jobs
	jobs="$(nproc 2>/dev/null || echo 4)"
	cmake --build "${build}" --target "${target}" -j"${jobs}"
}
