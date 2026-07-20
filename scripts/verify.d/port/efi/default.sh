#!/usr/bin/env bash
# QEMU + OVMF smoke: boot metal.efi, expect banner + memory lines.
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

# Headless doom auto-quit build (keep interactive build/efi/doom untouched).
# ESP marker autostart makes shell_init run it (interactive has no marker).
METAL_DOOM_MAX_TICKS=120 \
	METAL_DOOM_OUT_DIR="${ROOT}/build/efi/doom-verify" \
	"${ROOT}/scripts/build.d/port/efi/doom.sh"
printf '1\n' >"${ROOT}/build/efi/doom-verify/autostart"

pm_metal_efi_stage_esp "${EFI}" "${ESP}" "${ROOT}/build/efi/doom-verify"

# Shell stays in poll forever; stop QEMU once markers land (timeout = safety net).
# --foreground: Ctrl-C reaches QEMU. Own ESP so interactive ./scripts/run efi is safe.
rm -f "${LOG}"
echo "verify-efi: qemu headless (serial → ${LOG})" >&2
qemu-system-x86_64 \
	-machine q35,accel=kvm:tcg \
	-smp 4 \
	-m 512 \
	-display none \
	-audio none \
	-serial file:"${LOG}" \
	-drive if=pflash,format=raw,readonly=on,file="${OVMF}" \
	-drive format=raw,file=fat:rw:"${ESP}" \
	-boot order=d \
	&
qpid=$!

deadline=$((SECONDS + 180))
ok=0
while kill -0 "${qpid}" 2>/dev/null; do
	if [[ -s "${LOG}" ]] \
		&& grep -q "metal-shell: ok" "${LOG}" \
		&& grep -q "metal-doom: ok" "${LOG}"
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
grep -q "metal-time: ok" "${LOG}" || {
	echo "verify-efi: missing metal-time: ok" >&2
	exit 1
}
grep -q "metal-coro: yield ok" "${LOG}" || {
	echo "verify-efi: missing metal-coro: yield ok" >&2
	exit 1
}
grep -q "metal-coro: load ok" "${LOG}" || {
	echo "verify-efi: missing metal-coro: load ok" >&2
	exit 1
}
grep -q "metal-coro: ok" "${LOG}" || {
	echo "verify-efi: missing metal-coro: ok" >&2
	exit 1
}
grep -q "metal-task: ok" "${LOG}" || {
	echo "verify-efi: missing metal-task: ok" >&2
	exit 1
}
grep -q "metal-run: parallel join ok" "${LOG}" || {
	echo "verify-efi: missing metal-run: parallel join ok" >&2
	exit 1
}
grep -q "metal-run: enter/leave ok" "${LOG}" || {
	echo "verify-efi: missing metal-run: enter/leave ok" >&2
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
grep -q "metal-gfx: ok" "${LOG}" || {
	echo "verify-efi: missing metal-gfx: ok" >&2
	exit 1
}
grep -q "metal-ui: ok" "${LOG}" || {
	echo "verify-efi: missing metal-ui: ok" >&2
	exit 1
}
grep -q "metal-shell: ok" "${LOG}" || {
	echo "verify-efi: missing metal-shell: ok" >&2
	exit 1
}
grep -q "metal-wasm: ok" "${LOG}" || {
	echo "verify-efi: missing metal-wasm: ok" >&2
	exit 1
}
grep -q "metal-wasm: t0_hello ok" "${LOG}" || {
	echo "verify-efi: missing metal-wasm: t0_hello ok" >&2
	exit 1
}
grep -q "metal-async: sleep ok" "${LOG}" || {
	echo "verify-efi: missing metal-async: sleep ok" >&2
	exit 1
}
grep -q "metal-doom: ok" "${LOG}" || {
	echo "verify-efi: missing metal-doom: ok" >&2
	exit 1
}

echo "verify-efi: ok"
