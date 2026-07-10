#!/usr/bin/env bash
# Pack zephyr.efi into a partitioned FAT disk for VirtualBox (EFI boot).
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
	echo "usage: $0 <zephyr.efi> [output.img]" >&2
	exit 1
fi

EFI_SRC="$(readlink -f "$1")"
OUT_IMG="${2:-$(dirname "$EFI_SRC")/pymetal-zephyr-efi.img}"
OUT_VDI="${OUT_IMG%.img}.vdi"

if [[ ! -f "${EFI_SRC}" ]]; then
	echo "missing EFI file: ${EFI_SRC}" >&2
	exit 1
fi

for tool in mkfs.vfat sfdisk; do
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "missing ${tool}" >&2
		echo "  hint: sudo apt install dosfstools util-linux" >&2
		exit 1
	fi
done

IMG_SIZE_MB=64
PART_OFFSET_SECTORS=2048
PART_OFFSET_BYTES=$((PART_OFFSET_SECTORS * 512))
WORKDIR="$(mktemp -d)"
trap 'rm -rf "${WORKDIR}"' EXIT

IMG_PATH="${WORKDIR}/disk.img"

dd if=/dev/zero of="${IMG_PATH}" bs=1M count="${IMG_SIZE_MB}" status=none
printf 'label: dos\nunit: sectors\nstart=%s, type=c, bootable\n' "${PART_OFFSET_SECTORS}" \
	| sfdisk "${IMG_PATH}" >/dev/null
mkfs.vfat -F 32 -n PYMETAL --offset="${PART_OFFSET_SECTORS}" "${IMG_PATH}" >/dev/null

pack_with_mtools() {
	local part="@@1M"
	mmd -i "${IMG_PATH}${part}" ::EFI
	mmd -i "${IMG_PATH}${part}" ::EFI/BOOT
	mcopy -i "${IMG_PATH}${part}" "${EFI_SRC}" ::EFI/BOOT/bootx64.efi
	mcopy -i "${IMG_PATH}${part}" "${EFI_SRC}" ::zephyr.efi
}

pack_with_pyfatfs() {
	local root
	root="$(cd "$(dirname "$0")/.." && pwd)"
	"${root}/.venv/bin/python3" - "${IMG_PATH}" "${EFI_SRC}" "${PART_OFFSET_BYTES}" <<'PY'
import sys
from pathlib import Path

from pyfatfs.PyFatFS import PyFatFS

img, efi, offset = sys.argv[1], Path(sys.argv[2]), int(sys.argv[3])
data = efi.read_bytes()

fs = PyFatFS(img, offset=offset)
try:
    fs.makedirs("/EFI/BOOT", recreate=True)
    with fs.open("/EFI/BOOT/bootx64.efi", "wb") as f:
        f.write(data)
    with fs.open("/zephyr.efi", "wb") as f:
        f.write(data)
finally:
    fs.close()
PY
}

if command -v mmd >/dev/null 2>&1 && command -v mcopy >/dev/null 2>&1; then
	pack_with_mtools
elif [[ -x "$(cd "$(dirname "$0")/.." && pwd)/.venv/bin/python3" ]]; then
	pack_with_pyfatfs
else
	echo "missing mtools (mmd/mcopy) and project venv python for pyfatfs fallback" >&2
	echo "  hint: sudo apt install mtools  OR  pip install pyfatfs in .venv" >&2
	exit 1
fi

mkdir -p "$(dirname "${OUT_IMG}")"
cp "${IMG_PATH}" "${OUT_IMG}"

if command -v qemu-img >/dev/null 2>&1; then
	qemu-img convert -f raw -O vdi -o static "${IMG_PATH}" "${OUT_VDI}"
else
	OUT_VDI=""
fi

cat <<EOF
packed VirtualBox/QEMU-UEFI disk image:
  ${OUT_IMG}$( [[ -n "${OUT_VDI}" ]] && printf '\n  %s (use this in VirtualBox)' "${OUT_VDI}" )
  EFI/BOOT/bootx64.efi  (auto-boot)
  zephyr.efi            (manual: Shell> fs0:zephyr.efi)

VirtualBox:
  1. New VM, Type: Other/Unknown 64-bit
  2. Run: ./scripts/vbox-setup-vm.sh "<vm-name>" $( [[ -n "${OUT_VDI}" ]] && echo "${OUT_VDI}" || echo "${OUT_IMG}" )
     (uses OVMF instead of VirtualBox EFI — avoids CpuDxe #GP crashes)
  3. Or manually: Settings > System > Enable EFI, Base Memory >= 512 MiB, 1 CPU, ICH9 chipset
     then: VBoxManage setextradata "<vm>" "VBoxInternal/Devices/efi/0/Config/EfiRom" /usr/share/OVMF/OVMF_CODE_4M.fd
  4. Storage: attach $( [[ -n "${OUT_VDI}" ]] && echo "${OUT_VDI}" || echo "${OUT_IMG}" ) as SATA hard disk
  5. Serial: COM1, host pipe/file for console
  6. Start VM

  If you still see CpuDxe #GP with VirtualBox EFI:
  - Windows: run scripts/vbox-setup-vm.ps1 (needs OVMF — install QEMU for Windows or set OVMF_CODE)
  - Linux:   run scripts/vbox-setup-vm.sh, or add split_lock_detect=off to kernel cmdline
  - QEMU+OVMF is the reference boot path on all hosts

QEMU UEFI (needs ovmf; guest RAM >=256 MiB for UEFI loader + Zephyr):
  VARS=\$(mktemp) && cp /usr/share/OVMF/OVMF_VARS_4M.fd "\$VARS"
  qemu-system-x86_64 -m 512 \\
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \\
    -drive if=pflash,format=raw,file="\$VARS" \\
    -drive file=${OUT_IMG},format=raw,if=ide \\
    -serial stdio -display none
EOF
