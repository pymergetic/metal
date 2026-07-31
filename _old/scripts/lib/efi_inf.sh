# Generate EDK2 MetalPkg *.inf from *.inf.tpl (@METAL_ROOT@ → checkout root).
# Sourced by scripts/build.d/port/efi/default.sh (and bios for mbedtls source list).

pm_metal_efi_inf_generate() {
	local root="${1:-${ROOT:-}}"
	local pkg tpl inf
	if [[ -z "${root}" ]]; then
		echo "pm_metal_efi_inf_generate: ROOT unset" >&2
		return 1
	fi
	# Archived product package (live firmware is exp2 / forge).
	pkg="${root}/_old/src/efi/MetalPkg"
	for tpl in "${pkg}/Metal.inf.tpl" "${pkg}/DropbearGlue.inf.tpl"; do
		if [[ ! -f "${tpl}" ]]; then
			echo "pm_metal_efi_inf_generate: missing ${tpl}" >&2
			return 1
		fi
		inf="${tpl%.tpl}"
		# EDK2 IncPathFromBuildOptions only keeps -I paths that exist() as
		# literals; response-file expand also cannot use portable MODULE_DIR
		# for out-of-tree Metal. Bake absolute ROOT into the generated .inf.
		sed "s|@METAL_ROOT@|${root}|g" "${tpl}" >"${inf}"
	done
}
