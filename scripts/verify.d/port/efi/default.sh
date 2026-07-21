#!/usr/bin/env bash
# QEMU + OVMF: boot metal.efi, expect boot tree + ready (no auto-tests).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/efi-qemu.sh"

EFI="${ROOT}/build/efi/metal.efi"
# Separate from interactive ./scripts/run efi (build/efi/esp) — two QEMUs on the
# same fat:rw directory deadlock / stall with no serial output.
ESP="${ROOT}/build/efi/esp-verify"
LOG="${ROOT}/build/efi/qemu-verify.log"

OVMF="$(pm_metal_efi_ovmf)" || {
	echo "verify-efi: OVMF firmware not found (apt: ovmf)" >&2
	exit 1
}

pm_metal_efi_stage_esp "${EFI}" "${ESP}"
# Separate from interactive ./scripts/run efi (build/efi/vblk.img) — shared
# raw images lock under QEMU.
VBLK="$(pm_metal_efi_stage_vblk "${ROOT}/build/efi/vblk-verify.img")"

# Shell stays in poll forever; stop QEMU once markers land (timeout = safety net).
rm -f "${LOG}"
echo "verify-efi: qemu headless (serial → ${LOG})" >&2
qemu-system-x86_64 \
	-machine q35,accel=kvm:tcg \
	-smp 4 \
	-m 512 \
	-display none \
	-audiodev none,id=a0 \
	-netdev user,id=n0 \
	-device virtio-net-pci,netdev=n0 \
	-device virtio-sound-pci,audiodev=a0 \
	-drive if=none,id=vd0,format=raw,file="${VBLK}" \
	-device virtio-blk-pci,drive=vd0 \
	-serial file:"${LOG}" \
	-chardev null,id=vcon \
	-device virtio-serial-pci,max_ports=1 \
	-device virtconsole,chardev=vcon \
	-drive if=pflash,format=raw,readonly=on,file="${OVMF}" \
	-drive format=raw,file=fat:rw:"${ESP}" \
	-boot order=d \
	&
qpid=$!

deadline=$((SECONDS + 120))
ok=0
while kill -0 "${qpid}" 2>/dev/null; do
	if [[ -s "${LOG}" ]] \
		&& grep -q "pymergetic metal" "${LOG}" \
		&& grep -q "metal-boot: ready" "${LOG}" \
		&& grep -q "+-- ebs          ok" "${LOG}"
	then
		ok=1
		kill -KILL "${qpid}" 2>/dev/null || true
		break
	fi
	if (( SECONDS >= deadline )); then
		echo "verify-efi: qemu timed out (120s)" >&2
		kill -KILL "${qpid}" 2>/dev/null || true
		break
	fi
	sleep 0.25
done
wait "${qpid}" 2>/dev/null || true

if [[ ! -s "${LOG}" ]]; then
	echo "verify-efi: empty qemu log" >&2
	exit 1
fi
if [[ "${ok}" -ne 1 ]]; then
	echo "verify-efi: markers not seen before timeout" >&2
	echo "----- qemu serial -----"
	cat "${LOG}"
	echo "-----------------------"
	exit 1
fi

echo "----- qemu serial -----"
cat "${LOG}"
echo "-----------------------"

grep -q "pymergetic metal" "${LOG}" || {
	echo "verify-efi: missing boot tree root" >&2
	exit 1
}
grep -q "+-- mem" "${LOG}" || {
	echo "verify-efi: missing mem tree" >&2
	exit 1
}
grep -q "+-- devices" "${LOG}" || {
	echo "verify-efi: missing devices tree" >&2
	exit 1
}
grep -q "net/lwip+virtio-net" "${LOG}" || {
	echo "verify-efi: missing net/lwip+virtio-net" >&2
	exit 1
}
grep -q "audio/virtio-snd" "${LOG}" || {
	echo "verify-efi: missing audio/virtio-snd" >&2
	exit 1
}
grep -q "console/virtio-console" "${LOG}" || {
	echo "verify-efi: missing console/virtio-console" >&2
	exit 1
}
grep -q "blk/virtio-blk" "${LOG}" || {
	echo "verify-efi: missing blk/virtio-blk" >&2
	exit 1
}
grep -q "+-- ebs          ok" "${LOG}" || {
	echo "verify-efi: missing ebs ok" >&2
	exit 1
}
grep -q "|   +-- gfx      ok" "${LOG}" || {
	echo "verify-efi: missing gfx ok" >&2
	exit 1
}
grep -q "|   +-- ui       ok" "${LOG}" || {
	echo "verify-efi: missing ui ok" >&2
	exit 1
}
grep -q "|   +-- wasm     ok" "${LOG}" || {
	echo "verify-efi: missing wasm ok" >&2
	exit 1
}
grep -q "|   \`-- shell    ready" "${LOG}" || {
	echo "verify-efi: missing shell ready" >&2
	exit 1
}
grep -q "metal-boot: ready" "${LOG}" || {
	echo "verify-efi: missing metal-boot: ready" >&2
	exit 1
}

echo "verify-efi: ok"
