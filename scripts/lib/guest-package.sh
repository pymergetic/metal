# Guest-package staging tree — input to scripts/lib/guest-pkgs.sh.
# Final guest layout (embedded lz4 packages, applied at boot onto `/`):
#   /mods/tests/<name>.wasm          (pkg mods-tests)
#   /mods/apps/python.wasm           (pkg mods-apps-python)
#   /mods/apps/pm-test.py
#   /lib/python3.14/…                (pkg python-stdlib: Lib + folded wasi sysconfig)
#
# Staging still keeps wasi-lib/ separately; guest-pkgs folds it into python-stdlib.
# Prefer guest-pkgs compose + link embeds; install() is for ad-hoc hostdir debugging.
#
# Knobs (env, default 1):
#   PM_METAL_GUEST_TESTS   — include mods/tests/*
#   PM_METAL_APP_PYTHON    — include python.wasm + pm-test.py + stdlib deps
#   PM_METAL_FORCE_PACKAGE=1 — ignore stamp and rebuild package
#
# shellcheck shell=bash

: "${PM_METAL_GUEST_TESTS:=1}"
: "${PM_METAL_APP_PYTHON:=1}"

# Requires ROOT (via lib/root.sh or caller).
PM_GUEST_PACKAGE="${ROOT}/build/guest-package"
PM_GUEST_PACKAGE_TESTS="${PM_GUEST_PACKAGE}/mods/tests"
PM_GUEST_PACKAGE_APPS="${PM_GUEST_PACKAGE}/mods/apps"
PM_GUEST_PACKAGE_LIB="${PM_GUEST_PACKAGE}/lib/python3.14"
PM_GUEST_PACKAGE_WASI_LIB="${PM_GUEST_PACKAGE}/wasi-lib"
PM_GUEST_PACKAGE_STAMP="${PM_GUEST_PACKAGE}/.stamp"
PM_GUEST_BUILD_TESTS="${ROOT}/build/mods/tests"
PM_GUEST_PYTHON_WASM="${ROOT}/build/cpython/python.wasm"
PM_GUEST_PYTHON_LIB="${ROOT}/external/cpython/Lib"
PM_GUEST_PYTHON_WASI_LIB="${ROOT}/external/cpython/cross-build/wasm32-wasip1-threads/build/lib.wasi-wasm32-3.14"
PM_GUEST_PYTHON_TEST_PY="${ROOT}/mods/apps/python/pm-test.py"

pm_guest_package_python_deps_ok() {
	[[ -f "${PM_GUEST_PYTHON_WASM}" && -d "${PM_GUEST_PYTHON_LIB}" && -d "${PM_GUEST_PYTHON_WASI_LIB}" ]]
}

pm_guest_package_ensure_python() {
	if [[ "${PM_METAL_APP_PYTHON}" != "1" ]]; then
		return 0
	fi
	if ! pm_guest_package_python_deps_ok; then
		"${ROOT}/scripts/setup.d/deps/cpython.sh"
		"${ROOT}/scripts/setup.d/deps/tools.sh"
		"${ROOT}/scripts/build.d/guest/cpython.sh"
	fi
	if ! pm_guest_package_python_deps_ok; then
		echo "missing python.wasm or stdlib (PM_METAL_APP_PYTHON=1)" >&2
		return 1
	fi
}

# Compile test mods when enabled (build/mods/tests/*.wasm).
pm_guest_package_build_tests() {
	if [[ "${PM_METAL_GUEST_TESTS}" != "1" ]]; then
		return 0
	fi
	"${ROOT}/scripts/build.d/guest/mod.sh"
}

pm_guest_package_stamp_payload() {
	printf 'tests=%s python=%s\n' "${PM_METAL_GUEST_TESTS}" "${PM_METAL_APP_PYTHON}"
}

# True if package tree matches knobs and no inputs are newer than the stamp.
pm_guest_package_is_fresh() {
	local stamp="${PM_GUEST_PACKAGE_STAMP}"
	[[ "${PM_METAL_FORCE_PACKAGE:-0}" == "1" ]] && return 1
	[[ -f "${stamp}" ]] || return 1
	[[ "$(cat "${stamp}")" == "$(pm_guest_package_stamp_payload)" ]] || return 1

	if [[ "${PM_METAL_GUEST_TESTS}" == "1" ]]; then
		[[ -d "${PM_GUEST_PACKAGE_TESTS}" ]] || return 1
		compgen -G "${PM_GUEST_PACKAGE_TESTS}/*.wasm" >/dev/null || return 1
		# Source or compiled wasm newer than stamp → restage.
		if find "${ROOT}/mods/tests" -type f \( -name 'main.c' -o -name 'REACTOR' -o -name 'SOCKET' -o -name 'MOUNT' \) \
			-newer "${stamp}" -print -quit 2>/dev/null | grep -q .; then
			return 1
		fi
		if find "${PM_GUEST_BUILD_TESTS}" -name '*.wasm' -newer "${stamp}" -print -quit 2>/dev/null | grep -q .; then
			return 1
		fi
	else
		[[ ! -d "${PM_GUEST_PACKAGE_TESTS}" ]] || return 1
	fi

	if [[ "${PM_METAL_APP_PYTHON}" == "1" ]]; then
		[[ -f "${PM_GUEST_PACKAGE_APPS}/python.wasm" ]] || return 1
		[[ -f "${PM_GUEST_PACKAGE_APPS}/pm-test.py" ]] || return 1
		[[ -d "${PM_GUEST_PACKAGE_LIB}" ]] || return 1
		[[ -d "${PM_GUEST_PACKAGE_WASI_LIB}" ]] || return 1
		[[ -f "${PM_GUEST_PYTHON_WASM}" && -f "${PM_GUEST_PYTHON_TEST_PY}" ]] || return 1
		[[ "${PM_GUEST_PYTHON_WASM}" -nt "${stamp}" ]] && return 1
		[[ "${PM_GUEST_PYTHON_TEST_PY}" -nt "${stamp}" ]] && return 1
		if find "${PM_GUEST_PYTHON_LIB}" -type f ! -path '*/__pycache__/*' ! -name '*.pyc' \
			-newer "${stamp}" -print -quit 2>/dev/null | grep -q .; then
			return 1
		fi
		if find "${PM_GUEST_PYTHON_WASI_LIB}" -type f ! -path '*/__pycache__/*' \
			-newer "${stamp}" -print -quit 2>/dev/null | grep -q .; then
			return 1
		fi
	else
		[[ ! -e "${PM_GUEST_PACKAGE_APPS}/python.wasm" ]] || return 1
		[[ ! -d "${PM_GUEST_PACKAGE_LIB}" ]] || return 1
		[[ ! -d "${PM_GUEST_PACKAGE_WASI_LIB}" ]] || return 1
	fi
	return 0
}

# Restage build/guest-package/ from knobs (no-op when stamp is fresh).
pm_guest_package_compose() {
	pm_guest_package_build_tests
	pm_guest_package_ensure_python

	if pm_guest_package_is_fresh; then
		echo "guest-package: up to date (${PM_GUEST_PACKAGE}; tests=${PM_METAL_GUEST_TESTS} python=${PM_METAL_APP_PYTHON})"
		return 0
	fi

	rm -rf "${PM_GUEST_PACKAGE}"
	mkdir -p "${PM_GUEST_PACKAGE}/mods"

	if [[ "${PM_METAL_GUEST_TESTS}" == "1" ]]; then
		mkdir -p "${PM_GUEST_PACKAGE_TESTS}"
		local f
		shopt -s nullglob
		for f in "${PM_GUEST_BUILD_TESTS}"/*.wasm; do
			cp "${f}" "${PM_GUEST_PACKAGE_TESTS}/"
		done
		shopt -u nullglob
		if ! compgen -G "${PM_GUEST_PACKAGE_TESTS}/*.wasm" >/dev/null; then
			echo "guest-package: PM_METAL_GUEST_TESTS=1 but no wasm in ${PM_GUEST_BUILD_TESTS}" >&2
			return 1
		fi
	fi

	if [[ "${PM_METAL_APP_PYTHON}" == "1" ]]; then
		mkdir -p "${PM_GUEST_PACKAGE_APPS}" "${PM_GUEST_PACKAGE_LIB}" "${PM_GUEST_PACKAGE_WASI_LIB}"
		cp "${PM_GUEST_PYTHON_WASM}" "${PM_GUEST_PACKAGE_APPS}/python.wasm"
		if [[ ! -f "${PM_GUEST_PYTHON_TEST_PY}" ]]; then
			echo "missing ${PM_GUEST_PYTHON_TEST_PY}" >&2
			return 1
		fi
		cp "${PM_GUEST_PYTHON_TEST_PY}" "${PM_GUEST_PACKAGE_APPS}/pm-test.py"
		# Full stdlib — same tree on every platform (server-class; do not trim).
		# Keep __pycache__: pure-Python .pyc is what CPython loads on WASI too.
		rsync -a "${PM_GUEST_PYTHON_LIB}/" "${PM_GUEST_PACKAGE_LIB}/"
		rsync -a "${PM_GUEST_PYTHON_WASI_LIB}/" "${PM_GUEST_PACKAGE_WASI_LIB}/"
	fi

	pm_guest_package_stamp_payload >"${PM_GUEST_PACKAGE_STAMP}"
	echo "guest-package -> ${PM_GUEST_PACKAGE} (tests=${PM_METAL_GUEST_TESTS} python=${PM_METAL_APP_PYTHON})"
}

# Copy the full package into a VFS root (hostdir / hostfs tree).
# Callers must not cherry-pick — every platform gets the same tree for the
# active knobs (PM_METAL_GUEST_TESTS / PM_METAL_APP_*).
pm_guest_package_install() {
	local vfs_root="$1"
	if [[ ! -d "${PM_GUEST_PACKAGE}/mods" ]]; then
		echo "guest-package missing; call pm_guest_package_compose first" >&2
		return 1
	fi
	mkdir -p "${vfs_root}/mods"
	rm -rf "${vfs_root}/mods/tests" "${vfs_root}/mods/apps"
	if [[ -d "${PM_GUEST_PACKAGE}/mods/tests" ]]; then
		mkdir -p "${vfs_root}/mods/tests"
		cp -a "${PM_GUEST_PACKAGE}/mods/tests/." "${vfs_root}/mods/tests/"
	fi
	if [[ -d "${PM_GUEST_PACKAGE}/mods/apps" ]]; then
		mkdir -p "${vfs_root}/mods/apps"
		cp -a "${PM_GUEST_PACKAGE}/mods/apps/." "${vfs_root}/mods/apps/"
	fi

	rm -rf "${vfs_root}/lib/python3.14" "${vfs_root}/wasi-lib"
	if [[ -d "${PM_GUEST_PACKAGE_LIB}" ]]; then
		mkdir -p "${vfs_root}/lib"
		cp -a "${PM_GUEST_PACKAGE_LIB}" "${vfs_root}/lib/python3.14"
	fi
	if [[ -d "${PM_GUEST_PACKAGE_WASI_LIB}" ]]; then
		cp -a "${PM_GUEST_PACKAGE_WASI_LIB}" "${vfs_root}/wasi-lib"
	fi
}

# List test wasm basenames currently in the package (no path).
pm_guest_package_test_names() {
	local f
	shopt -s nullglob
	for f in "${PM_GUEST_PACKAGE_TESTS}"/*.wasm; do
		basename "${f}" .wasm
	done
	shopt -u nullglob
}
