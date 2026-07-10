#!/usr/bin/env bash
# Point a VirtualBox VM at OVMF instead of the built-in EFI firmware (avoids CpuDxe #GP).
set -euo pipefail

usage() {
	cat <<'EOF' >&2
usage: vbox-setup-vm.sh <vm-name> [disk.vdi]

Configure VirtualBox to boot pymetal with OVMF (same firmware QEMU uses).

Windows host: use scripts/vbox-setup-vm.ps1 in PowerShell instead.

Why: VirtualBox's built-in EFI often crashes in CpuDxe (#GP). OVMF avoids it.

Example (Linux / Git Bash):
  ./scripts/vbox-setup-vm.sh pymetal runtime/zephyr/images/pymetal-zephyr-efi.vdi
EOF
}

if [[ $# -lt 1 ]]; then
	usage
	exit 1
fi

if [[ "$(uname -s 2>/dev/null || true)" == MINGW* ]] || [[ "$(uname -s 2>/dev/null || true)" == MSYS* ]]; then
	echo "Windows host detected — use PowerShell:" >&2
	echo "  .\\scripts\\vbox-setup-vm.ps1 <vm-name> runtime\\zephyr\\images\\pymetal-zephyr-efi.vdi" >&2
	exit 1
fi

VM="$1"
DISK="${2:-}"

find_ovmf_code() {
	local candidate
	if [[ -n "${OVMF_CODE:-}" && -f "${OVMF_CODE}" ]]; then
		echo "${OVMF_CODE}"
		return 0
	fi
	for candidate in \
		/usr/share/OVMF/OVMF_CODE_4M.fd \
		/usr/share/OVMF/OVMF_CODE.fd \
		/usr/share/edk2/ovmf/OVMF_CODE.fd; do
		if [[ -f "${candidate}" ]]; then
			echo "${candidate}"
			return 0
		fi
	done
	return 1
}

if ! command -v VBoxManage >/dev/null 2>&1; then
	echo "VBoxManage not found; add VirtualBox to PATH" >&2
	exit 1
fi

if ! OVMF_CODE="$(find_ovmf_code)"; then
	echo "missing OVMF firmware; set OVMF_CODE or install ovmf package" >&2
	echo "  hint: sudo apt install ovmf" >&2
	exit 1
fi

VBoxManage showvminfo "${VM}" >/dev/null

VBoxManage modifyvm "${VM}" --firmware efi
VBoxManage modifyvm "${VM}" --memory 512
VBoxManage modifyvm "${VM}" --cpus 1
VBoxManage modifyvm "${VM}" --chipset ich9
VBoxManage modifyvm "${VM}" --ioapic on
VBoxManage setextradata "${VM}" "VBoxInternal/Devices/efi/0/Config/EfiRom" "${OVMF_CODE}"

if [[ -n "${DISK}" ]]; then
	DISK="$(readlink -f "${DISK}")"
	if [[ ! -f "${DISK}" ]]; then
		echo "missing disk image: ${DISK}" >&2
		exit 1
	fi
	if ! VBoxManage showvminfo "${VM}" --machinereadable | rg -q 'storagecontrollername0="SATA"'; then
		VBoxManage storagectl "${VM}" --name SATA --add sata --controller IntelAhci --portcount 2
	fi
	VBoxManage storageattach "${VM}" --storagectl SATA --port 0 --device 0 \
		--type hdd --medium "${DISK}" 2>/dev/null || true
fi

cat <<EOF
configured ${VM}:
  firmware : OVMF (${OVMF_CODE})
  memory   : 512 MiB
  cpus     : 1
  chipset  : ich9
$( [[ -n "${DISK}" ]] && echo "  disk     : ${DISK}" )

Power off the VM fully before first boot with OVMF.
Serial console: Settings > Ports > COM1 > host pipe or file.

Linux host only: if CpuDxe #GP persists, try split_lock_detect=off on kernel cmdline.
EOF
