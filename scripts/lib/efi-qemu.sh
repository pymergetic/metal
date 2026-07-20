# Shared EFI QEMU helpers — stage a clean ESP every launch.
# shellcheck shell=bash
# Source after ROOT is set (or source scripts/lib/root.sh first).

pm_metal_efi_ovmf() {
	local cand
	for cand in \
		/usr/share/ovmf/OVMF.fd \
		/usr/share/OVMF/OVMF.fd \
		/usr/share/OVMF/OVMF_CODE_4M.fd
	do
		if [[ -f "${cand}" ]]; then
			printf '%s\n' "${cand}"
			return 0
		fi
	done
	return 1
}

# Wipe + restage ESP from metal.efi. Always — never reuse a QEMU-dirtied tree.
# Also stages ESP packages from doom_dir (arg3 or METAL_DOOM_DIR or build/efi/doom).
pm_metal_efi_stage_esp() {
	local efi="$1"
	local esp="$2"
	local doom_dir="${3:-${METAL_DOOM_DIR:-${ROOT}/build/efi/doom}}"

	if [[ ! -f "${efi}" ]]; then
		echo "efi-qemu: missing ${efi} — run ./scripts/build efi" >&2
		return 1
	fi

	rm -rf "${esp}"
	mkdir -p "${esp}/EFI/BOOT"
	cp -f "${efi}" "${esp}/EFI/BOOT/BOOTX64.EFI"

	if [[ -f "${doom_dir}/doom.wasm" && -f "${doom_dir}/doom1.wad" ]]; then
		mkdir -p "${esp}/mods/apps/doom"
		cp -f "${doom_dir}/doom.wasm" "${esp}/mods/apps/doom/doom.wasm"
		cp -f "${doom_dir}/doom1.wad" "${esp}/mods/apps/doom/doom1.wad"
		if [[ -f "${doom_dir}/autostart" ]]; then
			cp -f "${doom_dir}/autostart" "${esp}/mods/apps/doom/autostart"
			echo "efi-qemu: staged mods/apps/doom/{doom.wasm,doom1.wad,autostart} from ${doom_dir}" >&2
		else
			echo "efi-qemu: staged mods/apps/doom/{doom.wasm,doom1.wad} from ${doom_dir}" >&2
		fi
	fi
}
