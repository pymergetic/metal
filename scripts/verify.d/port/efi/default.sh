#!/usr/bin/env bash
# QEMU + OVMF smoke: boot metal.efi, expect banner + memory lines.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
EFI="${ROOT}/build/efi/metal.efi"
ESP="${ROOT}/build/efi/esp"
LOG="${ROOT}/build/efi/qemu.log"

if [[ ! -f "${EFI}" ]]; then
	echo "verify-efi: missing ${EFI} — run ./scripts/build efi" >&2
	exit 1
fi

OVMF=""
for cand in \
	/usr/share/ovmf/OVMF.fd \
	/usr/share/OVMF/OVMF.fd \
	/usr/share/OVMF/OVMF_CODE_4M.fd
do
	if [[ -f "${cand}" ]]; then
		OVMF="${cand}"
		break
	fi
done
if [[ -z "${OVMF}" ]]; then
	echo "verify-efi: OVMF firmware not found (apt: ovmf)" >&2
	exit 1
fi

rm -rf "${ESP}"
mkdir -p "${ESP}/EFI/BOOT"
cp -f "${EFI}" "${ESP}/EFI/BOOT/BOOTX64.EFI"

# App calls ResetSystem(EfiResetShutdown); timeout is a safety net only.
rm -f "${LOG}"
timeout --signal=KILL 30s qemu-system-x86_64 \
	-machine q35,accel=kvm:tcg \
	-smp 4 \
	-m 512 \
	-display none \
	-serial file:"${LOG}" \
	-drive if=pflash,format=raw,readonly=on,file="${OVMF}" \
	-drive format=raw,file=fat:rw:"${ESP}" \
	-boot order=d \
	|| true

if [[ ! -s "${LOG}" ]]; then
	echo "verify-efi: empty qemu log" >&2
	exit 1
fi

echo "----- qemu serial -----"
cat "${LOG}"
echo "-----------------------"

grep -q "pymergetic efi" "${LOG}" || {
	echo "verify-efi: missing banner" >&2
	exit 1
}
grep -q "Total memory:" "${LOG}" || {
	echo "verify-efi: missing Total memory line" >&2
	exit 1
}
grep -q "metal-mem: ok" "${LOG}" || {
	echo "verify-efi: missing metal-mem: ok" >&2
	exit 1
}
grep -q "metal-coro: ok" "${LOG}" || {
	echo "verify-efi: missing metal-coro: ok" >&2
	exit 1
}
grep -q "metal-run: ok" "${LOG}" || {
	echo "verify-efi: missing metal-run: ok" >&2
	exit 1
}
grep -q "metal-efi: ok" "${LOG}" || {
	echo "verify-efi: missing ok marker" >&2
	exit 1
}

echo "verify-efi: ok"
