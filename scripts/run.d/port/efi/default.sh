#!/usr/bin/env bash
# Interactive QEMU+OVMF: always restage ESP, fat:rw.
# Display: VNC (default, remote) or GTK/SDL window (local desktop).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/efi-qemu.sh"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/run-tree.sh"

EFI="${ROOT}/build/x86_64_efi/metal.efi"
ESP="${ROOT}/build/x86_64_efi/esp"
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
		echo "  --vnc N   headless + VNC on display N (default :1 -> port 5901)" >&2
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

pm_metal_run_tree_begin "pymergetic metal run"
# Everything under here is the FAT ESP (not baked into metal.efi).
pm_metal_run_tree "+-- esp"

# Quiet per-file compile_templates / ext-apps chatter; tree summarizes.
export PM_METAL_STAGE_QUIET=1
pm_metal_efi_stage_esp "${EFI}" "${ESP}"
VBLK="$(pm_metal_efi_stage_vblk)"

n_tpl=0
if [[ -d "${ROOT}/mods/api/templates" ]]; then
	n_tpl="$(find "${ROOT}/mods/api/templates" -maxdepth 1 -type f -name '*.html' | wc -l | tr -d ' ')"
fi
pm_metal_run_tree "|   +-- templates  ok  ${n_tpl} html"

mods_bits=()
for d in py httpd api templates microdot utemplate www; do
	if [[ -d "${ESP}/mods/${d}" ]]; then
		mods_bits+=("${d}")
	fi
done
if [[ "${#mods_bits[@]}" -gt 0 ]]; then
	pm_metal_run_tree "|   +-- mods       ok  ${mods_bits[*]}"
else
	pm_metal_run_tree "|   +-- mods       -"
fi

app_bits=()
app_hints=()
if [[ -d "${ESP}/mods/apps" ]]; then
	shopt -s nullglob
	for app_dir in "${ESP}"/mods/apps/*/; do
		app_name="$(basename "${app_dir}")"
		app_bits+=("${app_name}")
		if compgen -G "${app_dir}${app_name}.*.aot" >/dev/null || [[ -f "${app_dir}${app_name}.wasm" ]]; then
			app_hints+=("${app_name}")
		fi
	done
	shopt -u nullglob
fi
if [[ "${#app_bits[@]}" -gt 0 ]]; then
	pm_metal_run_tree "|   +-- apps       ok  ${app_bits[*]}  (ESP sidecar, not in efi)"
	if [[ "${#app_hints[@]}" -gt 0 ]]; then
		local_i=0
		local_n="${#app_hints[@]}"
		for app_name in "${app_hints[@]}"; do
			local_i=$((local_i + 1))
			if [[ "${local_i}" -lt "${local_n}" ]]; then
				pm_metal_run_tree "|   |   +-- ${app_name}     run ${app_name} (aot/wasm)"
			else
				pm_metal_run_tree "|   |   \`-- ${app_name}     run ${app_name} (aot/wasm)"
			fi
		done
	fi
else
	pm_metal_run_tree "|   +-- apps       -    (METAL_EXT_APPS=name=dir -> ESP)"
fi

pm_metal_run_tree "|   \`-- BOOTX64    ok  metal.efi -> ESP"

# Display / hostfwd / scanout for the qemu group.
display_note=""
scanout_note="stdvga"
case "${DISPLAY_BACKEND}" in
none)
	display_note="none (serial only)"
	;;
gtk | sdl)
	if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
		echo "run-efi: ${DISPLAY_BACKEND} needs DISPLAY/WAYLAND_DISPLAY (this shell has neither)." >&2
		echo "run-efi: use a local desktop, ssh -X/-Y, or omit --${DISPLAY_BACKEND} for VNC." >&2
		exit 1
	fi
	display_note="${DISPLAY_BACKEND}"
	;;
*)
	vnc_port=$((5900 + ${VNC#:}))
	if ss -lnt 2>/dev/null | grep -qE ":${vnc_port}\\b"; then
		echo "run-efi: port ${vnc_port} already in use -- kill the other QEMU or: --vnc 2" >&2
		exit 1
	fi
	display_note="vnc *:${vnc_port}"
	;;
esac

if [[ "${METAL_SCANOUT_VIRTIO_GPU:-0}" == "1" ]]; then
	scanout_note="virtio-vga"
fi

pm_metal_run_tree "+-- qemu"
pm_metal_run_tree "|   +-- display    ${display_note}"
pm_metal_run_tree "|   +-- hostfwd    ssh :2222  http :8000  https :8443"
pm_metal_run_tree "|   \`-- scanout    ${scanout_note}"
pm_metal_run_tree_handover

args=(
	qemu-system-x86_64
	-machine q35,accel=kvm:tcg
	-smp 4
	-m 512
	-audiodev none,id=a0
	-netdev user,id=n0,hostfwd=tcp::2222-:22,hostfwd=tcp::8000-:8000,hostfwd=tcp::8443-:8443
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

if [[ "${METAL_SCANOUT_VIRTIO_GPU:-0}" == "1" ]]; then
	args+=(-vga none -device virtio-vga)
else
	args+=(-vga std -global VGA.vgamem_mb=64)
fi

case "${DISPLAY_BACKEND}" in
none)
	args+=(-display none)
	;;
gtk | sdl)
	args+=(-display "${DISPLAY_BACKEND}")
	;;
*)
	args+=(-display none -vnc "0.0.0.0${VNC}")
	;;
esac

exec "${args[@]}"
