#!/usr/bin/env bash
# Live EFI QEMU smoke for doc/iface HTTP UI (docs/DOC_IFACE_PLAN.md Part III).
# Boots metal.efi with hostfwd :18000->:8000, waits for ready, curls JSON + HTML.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/efi-qemu.sh"

EFI="${ROOT}/build/efi/metal.efi"
ESP="${ROOT}/build/efi/esp-doc-iface"
LOG="${ROOT}/build/efi/qemu-doc-iface.log"
HTTP_PORT="${PM_METAL_DOC_IFACE_HTTP_PORT:-18000}"
VBLK="${ROOT}/build/efi/vblk-doc-iface.img"

OVMF="$(pm_metal_efi_ovmf)" || {
	echo "doc-iface-smoke: OVMF firmware not found (apt: ovmf)" >&2
	exit 1
}

pm_metal_efi_stage_esp "${EFI}" "${ESP}"
: >"${ESP}/mods/tests/autotest"
VBLK="$(pm_metal_efi_stage_vblk "${VBLK}")"

rm -f "${LOG}"
echo "doc-iface-smoke: qemu headless (serial -> ${LOG}, http localhost:${HTTP_PORT})" >&2
qemu-system-x86_64 \
	-machine q35,accel=kvm:tcg \
	-smp 4 \
	-m 512 \
	-display none \
	-audiodev none,id=a0 \
	-netdev "user,id=n0,restrict=off,hostfwd=tcp::${HTTP_PORT}-:8000" \
	-device virtio-net-pci,netdev=n0 \
	-device virtio-sound-pci,audiodev=a0 \
	-drive if=none,id=vd0,format=raw,file="${VBLK}" \
	-device virtio-blk-pci,drive=vd0 \
	-serial file:"${LOG}" \
	-chardev null,id=vcon \
	-device virtio-serial-pci,max_ports=1 \
	-device virtconsole,chardev=vcon \
	-device virtio-tablet-pci \
	-drive if=pflash,format=raw,readonly=on,file="${OVMF}" \
	-drive format=raw,file=fat:rw:"${ESP}" \
	-boot order=d \
	&
qpid=$!
cleanup() {
	kill -KILL "${qpid}" 2>/dev/null || true
	wait "${qpid}" 2>/dev/null || true
}
trap cleanup EXIT

deadline=$((SECONDS + 180))
ready=0
while kill -0 "${qpid}" 2>/dev/null; do
	if [[ -s "${LOG}" ]] && grep -q "+-- ready        ok" "${LOG}"; then
		ready=1
		break
	fi
	if ((SECONDS >= deadline)); then
		echo "doc-iface-smoke: boot timed out (180s)" >&2
		tail -80 "${LOG}" || true
		exit 1
	fi
	sleep 0.25
done
[[ "${ready}" -eq 1 ]] || {
	echo "doc-iface-smoke: guest died before ready" >&2
	exit 1
}

# httpd starts after DHCP; give the stack a few seconds, then poll /health.
http_ok=0
for _ in $(seq 1 60); do
	if curl -fsS --max-time 2 "http://127.0.0.1:${HTTP_PORT}/health" | grep -q ok; then
		http_ok=1
		break
	fi
	sleep 1
done
[[ "${http_ok}" -eq 1 ]] || {
	echo "doc-iface-smoke: /health never answered on :${HTTP_PORT}" >&2
	tail -120 "${LOG}" || true
	exit 1
}

fail=0
check() {
	local path="$1"
	local needle="$2"
	local body
	body="$(curl -fsS --max-time 10 "http://127.0.0.1:${HTTP_PORT}${path}")" || {
		echo "doc-iface-smoke: FAIL GET ${path}" >&2
		fail=1
		return
	}
	if ! printf '%s' "${body}" | grep -F -- "${needle}" >/dev/null; then
		echo "doc-iface-smoke: FAIL ${path} missing '${needle}'" >&2
		echo "${body}" | head -c 400 >&2
		echo >&2
		fail=1
		return
	fi
	echo "doc-iface-smoke: ok ${path}"
}

check "/health" "ok"
check "/api/doc?limit=5" '"kind"'
check "/api/iface" "metal.guest"
check "/api/iface/pkg" "metal.guest"
check "/api/iface/sym?module=pymergetic.metal.fs&name=pm_metal_fs_open_async" "doc_key"
check "/" "<title>home - metal</title>"
check "/docs?limit=5" "<h1>docs</h1>"
check "/iface" "metal.guest"
check "/iface/sym?module=pymergetic.metal.fs&limit=10" "pm_metal_fs"
check "/static/doc.css" "doc/iface HTTP UI"

# Shell-catalog proof via Python face over HTTP (same tables help/pmcmd use).
check "/api/doc?kind=shell&limit=10" '"kind"'
check "/api/doc/key/py:pymergetic.metal.fs.open" "pymergetic.metal.fs.open"
check "/docs/key/py:pymergetic.metal.fs.open" "pymergetic.metal.fs.open"
check "/api/about" '"version"'
check "/api/externals?limit=5" '"id"'
check "/about" "<h1>about</h1>"
check "/externals?limit=5" "<h1>externals</h1>"
check "/api/limits?limit=5" '"id"'
check "/api/limits/net.asgi.ASGI_IO_MAX" "ASGI_IO_MAX"
check "/limits?limit=5" "<h1>limits</h1>"
check "/limits/net.asgi.ASGI_IO_MAX" "ASGI_IO_MAX"

if [[ "${fail}" -ne 0 ]]; then
	echo "doc-iface-smoke: FAILED" >&2
	exit 1
fi

echo "doc-iface-smoke: ok"
