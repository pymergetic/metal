# shellcheck shell=bash
# MicroPython embed generate (shared metal/py; ports only link).

pm_metal_upy_root() {
	if [[ -n "${ROOT:-}" ]]; then
		echo "${ROOT}"
	else
		echo "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
	fi
}

pm_metal_upy_embed_dir() {
	echo "$(pm_metal_upy_root)/build/micropython_embed"
}

pm_metal_upy_ensure_external() {
	local root
	root="$(pm_metal_upy_root)"
	if [[ ! -d "${root}/external/micropython/py" ]]; then
		"${root}/_old/scripts/setup" micropython
	fi
}

pm_metal_upy_generate_embed() {
	local root
	root="$(pm_metal_upy_root)"
	pm_metal_upy_ensure_external
	mkdir -p "${root}/build"
	# Config changes must refresh genhdr/moduledefs (stale table → link errors).
	if [[ "${PM_METAL_UPY_FORCE:-0}" == "1" ]] ||
		[[ ! -f "${root}/build/micropython_embed/genhdr/moduledefs.h" ]] ||
		[[ "${root}/src/pymergetic/metal/py/embed/mpconfigport.h" -nt \
			"${root}/build/micropython_embed/genhdr/moduledefs.h" ]]; then
		rm -rf "${root}/build/micropython-embed-build" \
			"${root}/build/micropython_embed"
	fi
	make -C "${root}/src/pymergetic/metal/py/embed" -f micropython_embed.mk \
		MICROPYTHON_TOP="${root}/external/micropython" \
		BUILD="${root}/build/micropython-embed-build" \
		PACKAGE_DIR="${root}/build/micropython_embed" \
		all
	rm -f "$(pm_metal_upy_embed_dir)/port/mphalport.c"
	echo "micropython: embed -> $(pm_metal_upy_embed_dir)"
}

# List embed .c to link. Optional arch: x86_64 | i386 — drops other-ISA asm/nlr/emit.
pm_metal_upy_embed_c_sources() {
	local d arch="${1:-}" f base
	d="$(pm_metal_upy_embed_dir)"
	[[ -d "${d}/py" ]] || return 1
	while IFS= read -r f; do
		base="$(basename "${f}")"
		case "${base}" in
		mphalport.c) continue ;;
		asmarm.c | asmrv32.c | asmthumb.c | asmxtensa.c) continue ;;
		asmx64.c) [[ "${arch}" == "i386" ]] && continue ;;
		asmx86.c) [[ "${arch}" == "x86_64" || "${arch}" == "" ]] && continue ;;
		emitnarm.c | emitnrv32.c | emitnthumb.c | emitnxtensa.c | emitnxtensawin.c) continue ;;
		emitnx64.c) [[ "${arch}" == "i386" ]] && continue ;;
		emitnx86.c) [[ "${arch}" == "x86_64" || "${arch}" == "" ]] && continue ;;
		emitinlinethumb.c | emitinlinextensa.c) continue ;;
		nlraarch64.c | nlrmips.c | nlrpowerpc.c | nlrrv32.c | nlrrv64.c | nlrthumb.c | nlrxtensa.c) continue ;;
		nlrx64.c) [[ "${arch}" == "i386" ]] && continue ;;
		nlrx86.c) [[ "${arch}" == "x86_64" || "${arch}" == "" ]] && continue ;;
		esac
		printf '%s\n' "${f}"
	done < <(find "${d}/py" "${d}/shared" "${d}/port" -name '*.c' 2>/dev/null | sort)
}
