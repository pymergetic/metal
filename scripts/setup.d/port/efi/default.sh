#!/usr/bin/env bash
# EFI / freestanding Metal — toolchain notes (no heavy fetch yet).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"

echo "efi setup (scaffold)"
echo "  target: UEFI x86_64 application (metal.efi)"
echo "  drivers: static virtio only (planned)"
echo "  tree:    ${ROOT}/src/efi"
echo
echo "Toolchain options (pick one later):"
echo "  - clang --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector"
echo "  - gnu-efi + gcc"
echo "  - EDK2 package (heavier)"
echo
echo "QEMU smoke (planned):"
echo "  qemu-system-x86_64 -bios OVMF.fd -drive format=raw,file=fat:rw:esp ..."
echo
if command -v clang >/dev/null 2>&1; then
	echo "found: clang $(clang --version | head -1)"
else
	echo "missing: clang (recommended for freestanding EFI)"
fi
if [[ -f /usr/share/ovmf/OVMF.fd ]] || [[ -f /usr/share/OVMF/OVMF_CODE.fd ]]; then
	echo "found: OVMF firmware"
else
	echo "missing: OVMF (Debian/Ubuntu: ovmf)"
fi
echo "efi setup: ok (scaffold)"
