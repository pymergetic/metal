#!/usr/bin/env bash
# EFI freestanding verify — stub until metal.efi + OVMF smoke exist.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"

if [[ ! -f "${ROOT}/build/efi/metal.efi" ]]; then
	echo "verify-efi: metal.efi not built yet — scaffold only (see src/efi/README.md)" >&2
	echo "verify-efi: skipped"
	exit 0
fi

echo "verify-efi: TODO QEMU+OVMF smoke with ${ROOT}/build/efi/metal.efi" >&2
exit 1
