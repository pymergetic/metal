#!/usr/bin/env bash
# Interactive QEMU+OVMF: always restage ESP, fat:rw.
# Display: VNC (default, remote) or GTK/SDL window (local desktop).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/efi-qemu.sh"

EFI="${ROOT}/build/efi/metal.efi"
ESP="${ROOT}/build/efi/esp"
VNC=":1"
DISPLAY_BACKEND="vnc" # vnc | gtk | sdl | none

while [[ $# -gt 0 ]]; do
	case "$1" in
	--vnc)
		DISPLAY_BACKEND="vnc"
		VNC=":$2"
		shift 2
		;;
	--vnc=*)
		DISPLAY_BACKEND="vnc"
		VNC=":${1#--vnc=}"
		shift
		;;
	--gtk)
		DISPLAY_BACKEND="gtk"
		shift
		;;
	--sdl)
		DISPLAY_BACKEND="sdl"
		shift
		;;
	--bench | --none)
		DISPLAY_BACKEND="none"
		shift
		;;
	--no-vnc)
		# Back-compat: local GTK window when possible.
		DISPLAY_BACKEND="gtk"
		shift
		;;
	-h | --help)
		echo "usage: scripts/run efi [--vnc N | --gtk | --sdl | --bench]" >&2
		echo "  --vnc N   headless + VNC on display N (default :1 → port 5901)" >&2
		echo "  --gtk     QEMU window via GTK (needs DISPLAY)" >&2
		echo "  --sdl     QEMU window via SDL (needs DISPLAY)" >&2
		echo "  --bench   -display none (serial only)" >&2
		exit 0
		;;
	*)
		echo "run-efi: unknown arg: $1" >&2
		exit 1
		;;
	esac
done

OVMF="$(pm_metal_efi_ovmf)" || {
	echo "run-efi: OVMF not found (apt: ovmf)" >&2
	exit 1
}

pm_metal_efi_stage_esp "${EFI}" "${ESP}"
VBLK="$(pm_metal_efi_stage_vblk)"

echo "run-efi: staged ${ESP}/EFI/BOOT/BOOTX64.EFI from ${EFI}" >&2
if [[ -f "${ESP}/mods/apps/doom/doom.x86_64.aot" ]]; then
	echo "run-efi: doom AOT on ESP → shell: run doom" >&2
elif [[ -f "${ESP}/mods/apps/doom/doom.wasm" ]]; then
	echo "run-efi: doom wasm on ESP → shell: run doom" >&2
else
	echo "run-efi: doom not on ESP (optional: METAL_DOOM_BUILD=1 ./scripts/build efi)" >&2
fi

args=(
	qemu-system-x86_64
	-machine q35,accel=kvm:tcg
	-smp 4
	-m 512
	-audiodev none,id=a0
	-netdev user,id=n0
	-device virtio-net-pci,netdev=n0
	-device virtio-sound-pci,audiodev=a0
	-drive if=none,id=vd0,format=raw,file="${VBLK}"
	-device virtio-blk-pci,drive=vd0
	-chardev null,id=vcon
	-device virtio-serial-pci,max_ports=1
	-device virtconsole,chardev=vcon
	-device virtio-tablet-pci
	-serial stdio
	-drive if=pflash,format=raw,readonly=on,file="${OVMF}"
	-drive format=raw,file=fat:rw:"${ESP}"
	-boot order=d
)

# Scanout: default Bochs stdvga flip; METAL_SCANOUT_VIRTIO_GPU=1 → virtio-vga.
if [[ "${METAL_SCANOUT_VIRTIO_GPU:-0}" == "1" ]]; then
	args+=(-vga none -device virtio-vga)
	echo "run-efi: scanout virtio-vga (metal virtio_gpu backend)" >&2
else
	# stdvga: VGA IS1 vblank + Bochs VBE page-flip; 64 MiB for 2× hi-res pages
	args+=(-vga std -global VGA.vgamem_mb=64)
fi

case "${DISPLAY_BACKEND}" in
none)
	args+=(-display none)
	echo "run-efi: display none (serial only)" >&2
	;;
gtk | sdl)
	if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
		echo "run-efi: ${DISPLAY_BACKEND} needs DISPLAY/WAYLAND_DISPLAY (this shell has neither)." >&2
		echo "run-efi: use a local desktop, ssh -X/-Y, or omit --${DISPLAY_BACKEND} for VNC." >&2
		exit 1
	fi
	args+=(-display "${DISPLAY_BACKEND}")
	echo "run-efi: display ${DISPLAY_BACKEND}" >&2
	;;
*)
	vnc_port=$((5900 + ${VNC#:}))
	if ss -lnt 2>/dev/null | grep -qE ":${vnc_port}\\b"; then
		echo "run-efi: port ${vnc_port} already in use — kill the other QEMU or: --vnc 2" >&2
		exit 1
	fi
	args+=(-display none -vnc "0.0.0.0${VNC}")
	echo "run-efi: VNC on *:${vnc_port}  → TightVNC to this host:${vnc_port} (keep this QEMU running)" >&2
	;;
esac

exec "${args[@]}"
