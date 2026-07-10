#!/usr/bin/env bash
# Build QEMU multiboot + EFI/VirtualBox images for pymergetic-metal runtime.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

QEMU_BUILD="${ROOT}/runtime/zephyr/build/qemu_x86_64"
QEMU_EFI_BUILD="${ROOT}/runtime/zephyr/build/qemu_x86_64_efi"
IMAGE_DIR="${ROOT}/runtime/zephyr/images"
EFI_CONF="${ROOT}/runtime/zephyr/boards/qemu_x86_64_efi.conf"

source "${ROOT}/.venv/bin/activate"
export ZEPHYR_BASE="${ROOT}/external/zephyr"

mkdir -p "${IMAGE_DIR}"

echo "=== build: qemu_x86_64 (multiboot — west/ninja run_qemu) ==="
west build -p always -b qemu_x86_64 runtime/zephyr --build-dir "${QEMU_BUILD}"

echo
echo "=== build: qemu_x86_64 (EFI — VirtualBox + QEMU UEFI) ==="
west build -p always -b qemu_x86_64 runtime/zephyr \
	--build-dir "${QEMU_EFI_BUILD}" \
	-- -DEXTRA_CONF_FILE="${EFI_CONF}"

EFI_BIN="${QEMU_EFI_BUILD}/zephyr/zephyr.efi"
if [[ ! -f "${EFI_BIN}" ]]; then
	echo "EFI build failed: missing ${EFI_BIN}" >&2
	exit 1
fi

echo
echo "=== pack: VirtualBox / QEMU-UEFI FAT disk ==="
"${ROOT}/scripts/pack-vbox-disk.sh" "${EFI_BIN}" "${IMAGE_DIR}/pymetal-zephyr-efi.img"

cat <<EOF

images ready:
  QEMU multiboot elf     : ${QEMU_BUILD}/zephyr/zephyr.elf
  QEMU run helpers       : ${QEMU_BUILD}/zephyr/zephyr-qemu-{locore,main}.elf (after -t run)
  QEMU multiboot run     : west build -d ${QEMU_BUILD} -t run
  EFI payload            : ${EFI_BIN}
  VBox/QEMU-UEFI disk    : ${IMAGE_DIR}/pymetal-zephyr-efi.img
  VirtualBox disk (VDI)  : ${IMAGE_DIR}/pymetal-zephyr-efi.vdi

QEMU UEFI (needs ovmf; use >=256 MiB guest RAM):
  VARS=\$(mktemp) && cp /usr/share/OVMF/OVMF_VARS_4M.fd "\$VARS"
  qemu-system-x86_64 -smp 2 -m 512 \\
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \\
    -drive if=pflash,format=raw,file="\$VARS" \\
    -drive file=${IMAGE_DIR}/pymetal-zephyr-efi.img,format=raw,if=ide \\
    -serial stdio -display none

VirtualBox: attach ${IMAGE_DIR}/pymetal-zephyr-efi.vdi — Windows: .\\scripts\\vbox-setup-vm.ps1 <vm> <vdi>

EOF
