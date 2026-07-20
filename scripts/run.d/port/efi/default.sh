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
		# No guest display path — read metal-doom: N fps on serial.
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
		echo "  --gtk     QEMU window via GTK (needs DISPLAY; X11-over-SSH is slow)" >&2
		echo "  --sdl     QEMU window via SDL (needs DISPLAY)" >&2
		echo "  --bench   -display none; measure real fps on serial (no VNC/X11)" >&2
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

echo "run-efi: staged ${ESP}/EFI/BOOT/BOOTX64.EFI from ${EFI}" >&2

args=(
	qemu-system-x86_64
	-machine q35,accel=kvm:tcg
	-smp 4
	-m 512
	-audio none
	-serial stdio
	-drive if=pflash,format=raw,readonly=on,file="${OVMF}"
	-drive format=raw,file=fat:rw:"${ESP}"
	-boot order=d
)

case "${DISPLAY_BACKEND}" in
gtk | sdl)
	if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
		echo "run-efi: ${DISPLAY_BACKEND} needs DISPLAY/WAYLAND_DISPLAY (this shell has neither)." >&2
		echo "run-efi: use a local desktop, ssh -X/-Y, or omit --${DISPLAY_BACKEND} for VNC." >&2
		exit 1
	fi
	args+=(-display "${DISPLAY_BACKEND}")
	echo "run-efi: display ${DISPLAY_BACKEND} (real QEMU window — Ctrl reaches the guest)" >&2
	;;
*)
	args+=(-display none -vnc "${VNC}")
	echo "run-efi: VNC ${VNC}  (e.g. ssh -L 5901:127.0.0.1:5901 … → localhost:5901)" >&2
	echo "run-efi: tip: TightVNC steals Ctrl; fire with z/f or use --gtk on a machine with DISPLAY" >&2
	;;
esac

exec "${args[@]}"
