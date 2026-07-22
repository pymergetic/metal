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
: >"${ESP}/mods/tests/autotest"
# Separate from interactive ./scripts/run efi (build/efi/vblk.img) — shared
# raw images lock under QEMU.
VBLK="$(pm_metal_efi_stage_vblk "${ROOT}/build/efi/vblk-verify.img")"

# async_tftp: QEMU slirp built-in TFTP + DHCP next-server / bootfile.
TFTP_DIR="${ROOT}/build/efi/tftp-verify"
mkdir -p "${TFTP_DIR}"
printf 'metal-async-tftp\n' >"${TFTP_DIR}/metal-tftp-proof.txt"

# Shell stays in poll forever; stop QEMU once markers land (timeout = safety net).
rm -f "${LOG}"
echo_pid=""
if command -v socat >/dev/null 2>&1; then
	# async_net proof: guest TCP to 10.0.2.2:10007 (QEMU slirp → host).
	socat TCP-LISTEN:10007,reuseaddr,fork EXEC:'cat' >/dev/null 2>&1 &
	echo_pid=$!
elif command -v python3 >/dev/null 2>&1; then
	python3 - <<'PY' >/dev/null 2>&1 &
import socket
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", 10007))
s.listen(8)
while True:
    c, _ = s.accept()
    try:
        data = c.recv(4096)
        if data:
            c.sendall(data)
    finally:
        c.close()
PY
	echo_pid=$!
elif command -v ncat >/dev/null 2>&1; then
	ncat -l 0.0.0.0 10007 --keep-open --exec 'cat' >/dev/null 2>&1 &
	echo_pid=$!
fi
echo "verify-efi: qemu headless (serial → ${LOG})" >&2
qemu-system-x86_64 \
	-machine q35,accel=kvm:tcg \
	-smp 4 \
	-m 512 \
	-display none \
	-audiodev none,id=a0 \
	-netdev user,id=n0,restrict=off,tftp="${TFTP_DIR}",bootfile=metal-tftp-proof.txt \
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

deadline=$((SECONDS + 180))
ok=0
while kill -0 "${qpid}" 2>/dev/null; do
	if [[ -s "${LOG}" ]] \
		&& grep -q "pymergetic metal" "${LOG}" \
		&& grep -q "metal-boot: ready" "${LOG}" \
		&& grep -q "+-- ebs          ok" "${LOG}" \
		&& grep -q "metal-test: ok" "${LOG}"
	then
		ok=1
		kill -KILL "${qpid}" 2>/dev/null || true
		break
	fi
	if (( SECONDS >= deadline )); then
		echo "verify-efi: qemu timed out (180s)" >&2
		kill -KILL "${qpid}" 2>/dev/null || true
		break
	fi
	sleep 0.25
done
wait "${qpid}" 2>/dev/null || true
if [[ -n "${echo_pid}" ]]; then
	kill "${echo_pid}" 2>/dev/null || true
	wait "${echo_pid}" 2>/dev/null || true
fi

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
grep -q "net/loopback" "${LOG}" || {
	echo "verify-efi: missing net/loopback" >&2
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
grep -q "stream/virtio-console" "${LOG}" || {
	echo "verify-efi: missing stream/virtio-console" >&2
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
grep -q "|   +-- shell    ok" "${LOG}" || {
	echo "verify-efi: missing shell ok" >&2
	exit 1
}
grep -q "|   \`-- ready    ok" "${LOG}" || {
	echo "verify-efi: missing ready ok" >&2
	exit 1
}
grep -q "metal-boot: ready" "${LOG}" || {
	echo "verify-efi: missing metal-boot: ready" >&2
	exit 1
}
grep -q "metal-blk: lba0 ok" "${LOG}" || {
	echo "verify-efi: missing metal-blk: lba0 ok" >&2
	exit 1
}
grep -q "metal-async: http ok" "${LOG}" || {
	echo "verify-efi: missing metal-async: http ok" >&2
	exit 1
}
grep -q "metal-test: ok" "${LOG}" || {
	echo "verify-efi: missing metal-test: ok" >&2
	exit 1
}
grep -q "metal-async: blk ok" "${LOG}" || {
	echo "verify-efi: missing metal-async: blk ok" >&2
	exit 1
}
grep -q "metal-async: net ok" "${LOG}" || {
	echo "verify-efi: missing metal-async: net ok" >&2
	exit 1
}
grep -q "metal-async: tftp ok" "${LOG}" || {
	echo "verify-efi: missing metal-async: tftp ok" >&2
	exit 1
}
grep -q "metal-test: dhcp-boot tftp=" "${LOG}" || {
	echo "verify-efi: missing metal-test: dhcp-boot" >&2
	exit 1
}

echo "verify-efi: ok"
