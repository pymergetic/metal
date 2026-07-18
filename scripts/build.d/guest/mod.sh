#!/usr/bin/env bash
# Build mods/tests/*/main.c -> build/mods/tests/*.wasm (wasm32-wasip1-threads).
# Skips when PM_METAL_GUEST_TESTS=0. Rebuilds a mod only when inputs are newer.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/root.sh"
: "${PM_METAL_GUEST_TESTS:=1}"

if [[ "${PM_METAL_GUEST_TESTS}" != "1" ]]; then
	echo "mod: skipped (PM_METAL_GUEST_TESTS=${PM_METAL_GUEST_TESTS})"
	exit 0
fi

WASI_SDK="${ROOT}/.tools/wasi-sdk"
CLANG="${WASI_SDK}/bin/clang"
SYSROOT="${WASI_SDK}/share/wasi-sysroot"
OUT="${ROOT}/build/mods/tests"

TARGET=wasm32-wasip1-threads
MAX_MEMORY=4194304

WAMR_SOCKET_EXT_INC="${ROOT}/external/wamr/core/iwasm/libraries/lib-socket/inc"
WAMR_SOCKET_EXT_SRC="${ROOT}/external/wamr/core/iwasm/libraries/lib-socket/src/wasi/wasi_socket_ext.c"
WAMR_SOCKET_EXT_OBJ="${OUT}/.wasi_socket_ext.${TARGET}.o"

if [ ! -x "${CLANG}" ]; then
	echo "wasi-sdk clang not found at ${CLANG} (see scripts/setup.d/deps/tools.sh)" >&2
	exit 1
fi

mkdir -p "${OUT}"
built=0
skipped=0

mod_needs_rebuild() {
	local out="$1" src="$2" mod_dir="$3"
	[[ -f "${out}" ]] || return 0
	[[ "${src}" -nt "${out}" ]] && return 0
	local marker
	for marker in REACTOR SOCKET MOUNT; do
		if [[ -f "${mod_dir}${marker}" && "${mod_dir}${marker}" -nt "${out}" ]]; then
			return 0
		fi
	done
	return 1
}

for mod_dir in "${ROOT}"/mods/tests/*/; do
	name="$(basename "${mod_dir}")"
	src="${mod_dir}main.c"
	[ -f "${src}" ] || continue

	out="${OUT}/${name}.wasm"
	if ! mod_needs_rebuild "${out}" "${src}" "${mod_dir}"; then
		skipped=$((skipped + 1))
		continue
	fi

	echo "mod: ${name}"
	built=$((built + 1))

	extra_flags=()
	extra_srcs=()
	if [ -f "${mod_dir}REACTOR" ]; then
		extra_flags+=(-mexec-model=reactor)
	fi
	if [ -f "${mod_dir}SOCKET" ]; then
		extra_flags+=(-I "${WAMR_SOCKET_EXT_INC}")
		if [ ! -f "${WAMR_SOCKET_EXT_OBJ}" ] || [ "${WAMR_SOCKET_EXT_SRC}" -nt "${WAMR_SOCKET_EXT_OBJ}" ]; then
			echo "  (compiling vendored wasi_socket_ext.o, warnings not this codebase's own)"
			"${CLANG}" --target="${TARGET}" --sysroot="${SYSROOT}" -pthread -O2 -w \
				-I "${WAMR_SOCKET_EXT_INC}" \
				-c -o "${WAMR_SOCKET_EXT_OBJ}" "${WAMR_SOCKET_EXT_SRC}"
		fi
		extra_srcs+=("${WAMR_SOCKET_EXT_OBJ}")
	fi
	if [ -f "${mod_dir}MOUNT" ]; then
		extra_flags+=(-DPM_METAL_BUILD_KERNEL)
	fi

	"${CLANG}" --target="${TARGET}" --sysroot="${SYSROOT}" -pthread \
		-O2 -Wall -Wextra \
		-Wl,--max-memory="${MAX_MEMORY}" \
		-I "${ROOT}/include" \
		"${extra_flags[@]}" \
		-o "${out}" "${src}" "${extra_srcs[@]}"
done

echo "mods/tests -> ${OUT} (built=${built} skipped=${skipped})"
