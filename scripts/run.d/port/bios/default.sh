#!/usr/bin/env bash
# Interactive BIOS runner (SeaBIOS + Multiboot2 -kernel).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/bios-qemu.sh"

ELF="${ROOT}/build/bios/metal.boot.elf"
VBLK="$(pm_metal_bios_stage_vblk "${ROOT}/build/bios/vblk.img")"
VNC=1
VNC_DISPLAY=1

while [[ $# -gt 0 ]]; do
	case "$1" in
	--no-vnc) VNC=0; shift ;;
	--vnc)
		VNC=1
		VNC_DISPLAY="${2:-1}"
		shift 2
		;;
	*)
		echo "usage: scripts/run bios [--vnc N] [--no-vnc]" >&2
		exit 1
		;;
	esac
done

if [[ ! -f "${ELF}" ]]; then
	echo "run-bios: missing ${ELF} — run ./scripts/build bios" >&2
	exit 1
fi

extra=()
if [[ "${VNC}" -eq 1 ]]; then
	extra+=(-vnc ":${VNC_DISPLAY}")
	echo "run-bios: VNC :${VNC_DISPLAY} (localhost:$((5900 + VNC_DISPLAY)))" >&2
else
	extra+=(-display none)
fi

exec qemu-system-x86_64 \
	-machine q35,accel=kvm:tcg \
	-smp 4 \
	-m 512 \
	"${extra[@]}" \
	-audiodev none,id=a0 \
	-netdev user,id=n0 \
	-device virtio-net-pci,netdev=n0 \
	-device virtio-sound-pci,audiodev=a0 \
	-drive if=none,id=vd0,format=raw,file="${VBLK}" \
	-device virtio-blk-pci,drive=vd0 \
	-device isa-debug-exit,iobase=0x501,iosize=0x02 \
	-serial stdio \
	-chardev null,id=vcon \
	-device virtio-serial-pci,max_ports=1 \
	-device virtconsole,chardev=vcon \
	-device virtio-tablet-pci \
	-vga std \
	-global VGA.vgamem_mb=64 \
	-kernel "${ELF}"
