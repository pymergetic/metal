#!/usr/bin/env bash
# QEMU + SeaBIOS: boot metal.elf (Multiboot2), expect boot tree + ready.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/bios-qemu.sh"

ELF="${ROOT}/build/bios/metal.boot.elf"
LOG="${ROOT}/build/bios/qemu-verify.log"
VBLK="$(pm_metal_bios_stage_vblk "${ROOT}/build/bios/vblk-verify.img")"

if [[ ! -f "${ELF}" ]]; then
	echo "verify-bios: missing ${ELF} — run ./scripts/build bios" >&2
	exit 1
fi

rm -f "${LOG}"
echo "verify-bios: qemu headless (serial → ${LOG})" >&2
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
	-device isa-debug-exit,iobase=0x501,iosize=0x02 \
	-serial file:"${LOG}" \
	-chardev null,id=vcon \
	-device virtio-serial-pci,max_ports=1 \
	-device virtconsole,chardev=vcon \
	-vga std \
	-kernel "${ELF}" \
	&
qpid=$!

deadline=$((SECONDS + 120))
ok=0
while kill -0 "${qpid}" 2>/dev/null; do
	if [[ -s "${LOG}" ]] \
		&& grep -q "pymergetic metal" "${LOG}" \
		&& grep -q "metal-boot: ready" "${LOG}" \
		&& grep -q "+-- handoff      ok" "${LOG}"
	then
		ok=1
		kill -KILL "${qpid}" 2>/dev/null || true
		break
	fi
	if (( SECONDS >= deadline )); then
		echo "verify-bios: qemu timed out (120s)" >&2
		kill -KILL "${qpid}" 2>/dev/null || true
		break
	fi
	sleep 0.25
done
wait "${qpid}" 2>/dev/null || true

if [[ ! -s "${LOG}" ]]; then
	echo "verify-bios: empty qemu log" >&2
	exit 1
fi
if [[ "${ok}" -ne 1 ]]; then
	echo "verify-bios: markers not seen before timeout" >&2
	echo "----- qemu serial -----"
	cat "${LOG}"
	echo "-----------------------"
	exit 1
fi

echo "----- qemu serial -----"
cat "${LOG}"
echo "-----------------------"

grep -q "pymergetic metal" "${LOG}" || {
	echo "verify-bios: missing boot tree root" >&2
	exit 1
}
grep -q "+-- mem" "${LOG}" || {
	echo "verify-bios: missing mem tree" >&2
	exit 1
}
grep -q "+-- devices" "${LOG}" || {
	echo "verify-bios: missing devices tree" >&2
	exit 1
}
grep -q "net/lwip+virtio-net" "${LOG}" || {
	echo "verify-bios: missing net/lwip+virtio-net" >&2
	exit 1
}
grep -q "audio/virtio-snd" "${LOG}" || {
	echo "verify-bios: missing audio/virtio-snd" >&2
	exit 1
}
grep -q "stream/virtio-console" "${LOG}" || {
	echo "verify-bios: missing stream/virtio-console" >&2
	exit 1
}
grep -q "blk/virtio-blk" "${LOG}" || {
	echo "verify-bios: missing blk/virtio-blk" >&2
	exit 1
}
grep -q "+-- handoff      ok" "${LOG}" || {
	echo "verify-bios: missing handoff ok" >&2
	exit 1
}
grep -q "|   +-- gfx      ok" "${LOG}" || {
	echo "verify-bios: missing gfx ok" >&2
	exit 1
}
grep -q "|   +-- ui       ok" "${LOG}" || {
	echo "verify-bios: missing ui ok" >&2
	exit 1
}
grep -q "|   +-- wasm     ok" "${LOG}" || {
	echo "verify-bios: missing wasm ok" >&2
	exit 1
}
grep -q "|   +-- shell    ok" "${LOG}" || {
	echo "verify-bios: missing shell ok" >&2
	exit 1
}
grep -q "|   \`-- ready    ok" "${LOG}" || {
	echo "verify-bios: missing ready ok" >&2
	exit 1
}
grep -q "metal-boot: ready" "${LOG}" || {
	echo "verify-bios: missing metal-boot: ready" >&2
	exit 1
}

echo "verify-bios: ok"
