#!/usr/bin/env bash
# Build mods/*/main.c -> build/mods/*.wasm with wasi-sdk (wasm32-wasip1).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WASI_SDK="${ROOT}/.tools/wasi-sdk"
CLANG="${WASI_SDK}/bin/clang"
SYSROOT="${WASI_SDK}/share/wasi-sysroot"
OUT="${ROOT}/build/mods"

# WASI preview1's own socket extension (docs/RUNTIME.md "Sockets") is not
# plain wasi-libc — this wasi-sdk's own wasm32-wasip1 sys/socket.h leaves
# socket()/bind()/connect()/listen() undeclared entirely on this target
# (only declared under a `__wasilibc_unmodified_upstream`/`_use_wasip2`
# macro this target doesn't define). WAMR ships its own drop-in instead:
# lib-socket/inc/wasi_socket_ext.h (POSIX-shaped decls, backed by the
# non-standard wasi_snapshot_preview1 sock_* imports libc_wasi_wrapper.c
# implements) + lib-socket/src/wasi/wasi_socket_ext.c (the one small .c
# implementing them) — see external/wamr/samples/socket-api's own
# lib_socket_wasi.cmake for the upstream reference build of the same pair.
WAMR_SOCKET_EXT_INC="${ROOT}/external/wamr/core/iwasm/libraries/lib-socket/inc"
WAMR_SOCKET_EXT_SRC="${ROOT}/external/wamr/core/iwasm/libraries/lib-socket/src/wasi/wasi_socket_ext.c"
# Precompiled once (cached, not per-mod — same vendor source every time)
# to a plain .o, deliberately *without* this script's own -Wall -Wextra
# below: those flags are this codebase's own bar for its own code
# (main.c), not upstream WAMR's — never hand-edit a vendored file just to
# silence its own warnings (docs/SOURCETREE.md "Vendoring"), so the
# actual fix is compiling it separately instead, once, with warnings off
# for that one translation unit only. Rebuilt automatically if the
# vendored source itself is ever newer (a fresh scripts/setup-wamr.sh
# re-vendor, or a new patches/wamr/*.patch touching this file).
WAMR_SOCKET_EXT_OBJ="${OUT}/.wasi_socket_ext.o"

if [ ! -x "${CLANG}" ]; then
	echo "wasi-sdk clang not found at ${CLANG} (see scripts/setup-tools.sh)" >&2
	exit 1
fi

mkdir -p "${OUT}"

for mod_dir in "${ROOT}"/mods/*/; do
	name="$(basename "${mod_dir}")"
	src="${mod_dir}main.c"
	[ -f "${src}" ] || continue

	out="${OUT}/${name}.wasm"
	echo "mod: ${name}"

	# Most mods are ordinary wasi "command" modules (crt1 provides _start,
	# which calls main()). A mod meant to be depended on by another one via
	# runtime/runtime.c's own module_reader (see docs/RUNTIME.md
	# "Multi-module") must instead be a "reactor" (no _start of its own —
	# WAMR's own multi-module loader rejects a command module as a
	# sub-module, see wasm_loader.c's "can not be a sub-module") — opt in
	# with an empty mods/<name>/REACTOR marker file, checked here rather
	# than guessed from main()'s own presence/absence so a reactor mod can
	# still keep an ordinary main() for a standalone run (see
	# mods/t8_multimod_lib's own main.c).
	extra_flags=()
	extra_srcs=()
	if [ -f "${mod_dir}REACTOR" ]; then
		extra_flags+=(-mexec-model=reactor)
	fi
	# Opt in with an empty mods/<name>/SOCKET marker file (same convention
	# as REACTOR above) — pulls in wasi_socket_ext.h's own include dir and
	# links its one precompiled .o (see WAMR_SOCKET_EXT_OBJ above)
	# straight into this mod's own build, no separate static-lib step.
	if [ -f "${mod_dir}SOCKET" ]; then
		extra_flags+=(-I "${WAMR_SOCKET_EXT_INC}")
		if [ ! -f "${WAMR_SOCKET_EXT_OBJ}" ] || [ "${WAMR_SOCKET_EXT_SRC}" -nt "${WAMR_SOCKET_EXT_OBJ}" ]; then
			echo "  (compiling vendored wasi_socket_ext.o, warnings not this codebase's own)"
			"${CLANG}" --target=wasm32-wasip1 --sysroot="${SYSROOT}" -O2 -w \
				-I "${WAMR_SOCKET_EXT_INC}" \
				-c -o "${WAMR_SOCKET_EXT_OBJ}" "${WAMR_SOCKET_EXT_SRC}"
		fi
		extra_srcs+=("${WAMR_SOCKET_EXT_OBJ}")
	fi
	# Privileged mount()/umount() — empty mods/<name>/MOUNT marker opts into
	# -DPM_METAL_BUILD_KERNEL so include/pymergetic/metal/mount/mount.h is visible.
	# See docs/MOUNT.md Phase 5.
	if [ -f "${mod_dir}MOUNT" ]; then
		extra_flags+=(-DPM_METAL_BUILD_KERNEL)
	fi

	"${CLANG}" --target=wasm32-wasip1 --sysroot="${SYSROOT}" \
		-O2 -Wall -Wextra \
		-I "${ROOT}/include" \
		"${extra_flags[@]}" \
		-o "${out}" "${src}" "${extra_srcs[@]}"
done

echo "mods -> ${OUT}"
