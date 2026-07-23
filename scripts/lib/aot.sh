# Shared offline AOT helpers (wamrc → .aot). Any guest *can* be AOT;
# call sites opt in (doom, embed async_aot, marker file, …).
# shellcheck shell=bash

pm_metal_wamrc_bin() {
	printf '%s\n' "${METAL_WAMRC:-${ROOT}/.tools/wamrc/wamrc}"
}

pm_metal_wamrc_ready() {
	local bin

	bin="$(pm_metal_wamrc_bin)"
	[[ -x "${bin}" ]]
}

# Host AOT tags to build. Default = arches Metal ships today.
# Override when a port lands, e.g. METAL_AOT_ARCHS="x86_64 i386 aarch64".
: "${METAL_AOT_ARCHS:=x86_64 i386}"

# Compile wasm → aot with flags matching Metal EFI/BIOS runtime:
# software bounds checks (HW bound check disabled), no SIMD.
# Usage: pm_metal_aot_compile <in.wasm> <out.aot> <arch>
# Target is required — never default a single arch.
pm_metal_aot_compile() {
	local wasm="${1:-}"
	local aot="${2:-}"
	local target="${3:-}"
	local wamrc
	local tmp

	if [[ -z "${wasm}" || -z "${aot}" || -z "${target}" ]]; then
		echo "aot: usage: pm_metal_aot_compile <in.wasm> <out.aot> <arch>" >&2
		return 2
	fi
	case "${target}" in
	x86_64 | i386 | aarch64 | arm | thumb | riscv64 | riscv32) ;;
	*)
		echo "aot: unknown target ${target}" >&2
		return 2
		;;
	esac
	if [[ ! -f "${wasm}" ]]; then
		echo "aot: missing ${wasm}" >&2
		return 1
	fi
	if ! pm_metal_wamrc_ready; then
		echo "aot: wamrc missing — run: ./scripts/setup wamrc" >&2
		return 1
	fi

	wamrc="$(pm_metal_wamrc_bin)"
	mkdir -p "$(dirname "${aot}")"
	tmp="${aot}.tmp.$$"
	# Portable baseline for QEMU + iron.
	# opt-level 1: higher opts have emitted SSE that #GP on Metal EFI.
	# size-level 1 is fine on x86; aarch64 rejects it (code model) → 0.
	local size_level=1
	case "${target}" in
	aarch64 | riscv64 | riscv32) size_level=0 ;;
	esac
	if ! "${wamrc}" \
		--target="${target}" \
		--cpu=generic \
		--bounds-checks=1 \
		--disable-simd \
		--opt-level=1 \
		--size-level="${size_level}" \
		-o "${tmp}" \
		"${wasm}"
	then
		rm -f "${tmp}"
		echo "aot: wamrc failed for ${wasm} (target=${target})" >&2
		return 1
	fi
	mv -f "${tmp}" "${aot}"
	echo "aot: ok -> ${aot} (target=${target}, $(wc -c <"${aot}") bytes)" >&2
	return 0
}

# Emit <stem>.<arch>.aot for each METAL_AOT_ARCHS entry (never bare <stem>.aot).
# Signs each when PKI wants. Returns 0 if at least one arch succeeded.
pm_metal_aot_compile_all() {
	local wasm="${1:-}"
	local stem="${2:-}"
	local arch
	local aot
	local ok=0

	if [[ -z "${wasm}" || -z "${stem}" ]]; then
		echo "aot: usage: pm_metal_aot_compile_all <in.wasm> <out_stem>" >&2
		return 2
	fi

	# Kill legacy un-infixed names so wrong-width hosts never pick them up.
	rm -f "${stem}.aot" "${stem}.aot.sig"

	for arch in ${METAL_AOT_ARCHS}; do
		aot="${stem}.${arch}.aot"
		if pm_metal_aot_compile "${wasm}" "${aot}" "${arch}"; then
			pm_metal_aot_sign_or_clear "${aot}" || true
			ok=1
		else
			rm -f "${aot}" "${aot}.sig"
		fi
	done

	if [[ "${ok}" -eq 1 ]]; then
		return 0
	fi
	return 1
}

# Sign <file> → <file>.sig with Mods CA when trust wants signatures.
pm_metal_aot_sign_or_clear() {
	local file="${1:-}"
	local sig key

	# shellcheck disable=SC1091
	source "${ROOT}/scripts/lib/pki.sh"

	if [[ -z "${file}" || ! -f "${file}" ]]; then
		return 1
	fi

	sig="${file}.sig"
	key="$(pm_metal_pki_dir)/mods/ca.key"
	if pm_metal_pki_want_sign && [[ -f "${key}" ]]; then
		if "${ROOT}/scripts/pki" sign-wasm "${file}"; then
			echo "aot: signed ${sig}" >&2
			return 0
		fi
		echo "aot: sign failed — removing stale ${sig}" >&2
		rm -f "${sig}"
		return 1
	fi

	if [[ -f "${sig}" ]]; then
		echo "aot: clear .sig (trust mode=$(pm_metal_pki_trust_mode))" >&2
		rm -f "${sig}"
	fi
	return 0
}
