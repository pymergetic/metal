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
# Stages doom into mods/apps/doom/ when METAL_DOOM_BUILD=1 output (or METAL_DOOM_DIR) exists.
pm_metal_efi_stage_esp() {
	local efi="$1"
	local esp="$2"

	if [[ ! -f "${efi}" ]]; then
		echo "efi-qemu: missing ${efi} — run ./scripts/build efi" >&2
		return 1
	fi

	rm -rf "${esp}"
	mkdir -p "${esp}/EFI/BOOT"
	cp -f "${efi}" "${esp}/EFI/BOOT/BOOTX64.EFI"
	if [[ -f "${efi}.sig" ]]; then
		cp -f "${efi}.sig" "${esp}/EFI/BOOT/BOOTX64.EFI.sig"
	fi

	# Marker for embedded async_fs proof (Metal awaitable FS).
	mkdir -p "${esp}/mods/tests"
	printf 'metal-async-fs\n' >"${esp}/mods/tests/async_fs.txt"

	# shellcheck disable=SC1091
	source "${ROOT}/scripts/lib/doom.sh"
	if [[ -n "${3:-}" ]]; then
		METAL_DOOM_DIR="$3" pm_metal_doom_stage_into "${esp}" || true
	else
		pm_metal_doom_stage_into "${esp}" || true
	fi
}

# Raw virtio-blk image with LBA0 magic "METALBLK1".
pm_metal_efi_stage_vblk() {
	local img="${1:-${ROOT}/build/efi/vblk.img}"
	mkdir -p "$(dirname "${img}")"
	dd if=/dev/zero of="${img}" bs=1M count=8 status=none
	printf 'METALBLK1' | dd of="${img}" bs=1 seek=0 conv=notrunc status=none
	printf '%s\n' "${img}"
}
