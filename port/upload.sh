#!/usr/bin/env bash
# Put a firmware drop on iron. BIOS/UEFI: scp/tar to the TFTP/BOOTP host.
# RV1106 is Maskrom (make BOARD=ARMV7_RV1106 upload), not this script.
#
#   METAL_PXE_HOST      TFTP/BOOTP host (required; no default)
#   METAL_PXE_USER      default root
#   METAL_PXE_PATH      default /storage/tftp
#   METAL_EFI_PATH      default $METAL_PXE_PATH/efi
#   METAL_PXE_SSH_OPTS  extra ssh/scp flags
set -euo pipefail

PORT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
seat="${1:-}"
build="${2:-}"

die() {
	printf '%s\n' "$*" >&2
	exit 1
}

[[ -n "$seat" && -n "$build" ]] || die "usage: upload.sh bios|uefi BUILDDIR"

host="${METAL_PXE_HOST:?set METAL_PXE_HOST to the TFTP/BOOTP host}"
user="${METAL_PXE_USER:-root}"
dest="${METAL_PXE_PATH:-/storage/tftp}"
ssh_extra="${METAL_PXE_SSH_OPTS:-}"
target="${user}@${host}"

# OpenWrt often has no rsync/sftp — tar|ssh is the drop path.
remote_tar() {
	local src="$1" remote_dir="$2"
	# shellcheck disable=SC2086
	ssh ${ssh_extra} "$target" "mkdir -p '${remote_dir}'"
	# shellcheck disable=SC2086
	tar -C "$src" -cf - . | ssh ${ssh_extra} "$target" "tar -C '${remote_dir}' -xf -"
}

case "$seat" in
bios)
	[[ -f "$build/metal.qemu.elf" ]] || die "upload bios: missing $build/metal.qemu.elf (build first)"
	[[ -f "$PORT/netboot/metal.ipxe" ]] || die "upload bios: missing $PORT/netboot/metal.ipxe"
	stage="$(mktemp -d)"
	trap 'rm -rf "$stage"' EXIT
	cp "$build/metal.qemu.elf" "$stage/metal.elf"
	cp "$PORT/netboot/metal.ipxe" "$stage/metal.ipxe"
	printf 'upload bios: trampoline + metal.ipxe → %s:%s/\n' "$target" "$dest"
	remote_tar "$stage" "$dest"
	# shellcheck disable=SC2086
	ssh ${ssh_extra} "$target" "ls -la '${dest}/metal.elf' '${dest}/metal.ipxe'"
	printf 'upload bios: ok — DHCP NBP stays undionly.kpxe; iPXE runs metal.ipxe\n'
	;;
uefi)
	[[ -f "$build/esp/EFI/BOOT/BOOTX64.EFI" ]] || die "upload uefi: missing $build/esp/EFI/BOOT/BOOTX64.EFI (build first)"
	[[ -f "$PORT/netboot/metal-efi.ipxe" ]] || die "upload uefi: missing $PORT/netboot/metal-efi.ipxe"
	efi_dest="${METAL_EFI_PATH:-$dest/efi}"
	stage="$(mktemp -d)"
	trap 'rm -rf "$stage"' EXIT
	cp "$PORT/netboot/metal-efi.ipxe" "$stage/metal-efi.ipxe"
	printf 'upload uefi: ESP → %s:%s/  script → %s:%s/\n' "$target" "$efi_dest" "$target" "$dest"
	remote_tar "$build/esp" "$efi_dest"
	remote_tar "$stage" "$dest"
	# shellcheck disable=SC2086
	ssh ${ssh_extra} "$target" "ls -la '${efi_dest}/EFI/BOOT' '${dest}/metal-efi.ipxe'"
	printf 'upload uefi: ok — DHCP NBP stays ipxe.efi; iPXE runs metal-efi.ipxe\n'
	;;
*)
	die "upload.sh: seat $seat is not bios|uefi"
	;;
esac
