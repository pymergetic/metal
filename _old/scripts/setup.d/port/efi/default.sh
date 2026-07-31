#!/usr/bin/env bash
# EFI / freestanding Metal — OVMF + EDK2 readiness check.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"

echo "efi setup"
echo "  target: UEFI x86_64 application (metal.efi via EDK2)"
echo "  design: ${ROOT}/docs/EFI.md"
echo "  package: ${ROOT}/src/efi/MetalPkg"
echo

if [[ -x "${ROOT}/.tools/nasm/bin/nasm" ]]; then
	echo "found: $("${ROOT}/.tools/nasm/bin/nasm" -v)"
else
	echo "missing: .tools/nasm — run ./scripts/setup edk2"
fi
if [[ -d "${ROOT}/external/edk2/.git" ]]; then
	echo "found: edk2 $(git -C "${ROOT}/external/edk2" rev-parse --short HEAD)"
else
	echo "missing: external/edk2 — run ./scripts/setup edk2"
fi
if [[ -f /usr/share/ovmf/OVMF.fd ]] || [[ -f /usr/share/OVMF/OVMF.fd ]]; then
	echo "found: OVMF firmware"
else
	echo "missing: OVMF (Debian/Ubuntu: ovmf)"
fi
if command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "found: qemu-system-x86_64"
else
	echo "missing: qemu-system-x86_64"
fi
echo "efi setup: ok"
