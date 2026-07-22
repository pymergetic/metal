# Shared BIOS QEMU helpers — Multiboot2 -kernel (SeaBIOS).
# shellcheck shell=bash

pm_metal_bios_stage_vblk() {
	local img="${1:-${ROOT}/build/bios/vblk.img}"
	mkdir -p "$(dirname "${img}")"
	dd if=/dev/zero of="${img}" bs=1M count=8 status=none
	printf 'METALBLK1' | dd of="${img}" bs=1 seek=0 conv=notrunc status=none
	printf '%s\n' "${img}"
}
