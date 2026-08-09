#!/usr/bin/env bash
# PXE-boot MetalPython in QEMU (BIOS / UEFI) — images from CDN, not TFTP.
#
# Real path: DHCP → undionly.kpxe|ipxe.efi → metal.ipxe → CDN lead artifact.
# Ubuntu's stock QEMU iPXE ROM (2022) cannot finish TLS against modern CDNs;
# default HTTPS test uses bootserver/nbp/ipxe.lkrn (see build-nbp.sh).
#
# Usage:
#   ./qemu-pxe.sh                 # HTTPS official CDN + modern iPXE
#   ./qemu-pxe.sh --local         # HTTP host docker via 10.0.2.2:8000
#   ./qemu-pxe.sh --stock-rom     # force Ubuntu ROM (HTTPS often fails)
#   ./qemu-pxe.sh --uefi --local
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TPL="${ROOT}/metal.ipxe.tpl"
NBP_DIR="${ROOT}/nbp"
CDN_URL="${METAL_PXE_CDN_URL:-https://cdn.pymergetic.com/cdn}"
PROBE_URL=""
QEMU="${QEMU:-qemu-system-x86_64}"
OVMF="${OVMF:-/usr/share/ovmf/OVMF.fd}"
MODE=bios
TIMEOUT="${TIMEOUT:-90}"
STOCK_ROM=0
KEEP=0

while [[ $# -gt 0 ]]; do
	case "$1" in
	--local)
		CDN_URL="http://10.0.2.2:8000/cdn"
		PROBE_URL="http://127.0.0.1:8000/cdn"
		;;
	--uefi) MODE=uefi ;;
	--stock-rom) STOCK_ROM=1 ;;
	--cdn)
		CDN_URL="${2:-}"
		shift
		;;
	--timeout)
		TIMEOUT="${2:-90}"
		shift
		;;
	--keep) KEEP=1 ;;
	-h | --help)
		sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		echo "unknown arg: $1" >&2
		exit 2
		;;
	esac
	shift
done

CDN_URL="${CDN_URL%/}"
PROBE_URL="${PROBE_URL:-$CDN_URL}"
WORKDIR="$(mktemp -d)"
cleanup() {
	if [[ "${KEEP}" -eq 0 ]]; then
		rm -rf "${WORKDIR}"
	else
		echo "kept ${WORKDIR}" >&2
	fi
}
trap cleanup EXIT

TFTP="${WORKDIR}/tftp"
mkdir -p "${TFTP}"
LOG="${WORKDIR}/serial.log"

IMAGE_URL="${CDN_URL}/artifacts/lead/pymergetic.metal.arch.x86_64.elf"
IMAGE_EFI_URL="${CDN_URL}/artifacts/lead/pymergetic.metal.arch.x86_64.efi"
PROBE_ELF="${PROBE_URL}/artifacts/lead/pymergetic.metal.arch.x86_64.elf"

sed \
	-e "s|__NEXT_SERVER__|10.0.2.2|g" \
	-e "s|__IMAGE_URL__|${IMAGE_URL}|g" \
	-e "s|__IMAGE_EFI_URL__|${IMAGE_EFI_URL}|g" \
	-e "s|__CDN_URL__|${CDN_URL}|g" \
	"${TPL}" >"${TFTP}/metal.ipxe"

echo "qemu-pxe: CDN=${CDN_URL}" >&2
echo "qemu-pxe: mode=${MODE} timeout=${TIMEOUT}s" >&2
echo -n "qemu-pxe: probe lead … " >&2
if ! curl -sfI "${PROBE_ELF}" >/dev/null; then
	echo "FAIL (${PROBE_ELF})" >&2
	exit 1
fi
echo "ok" >&2

run_bios() {
	local lk="${NBP_DIR}/ipxe.lkrn"
	if [[ "${STOCK_ROM}" -eq 0 && -f "${lk}" ]]; then
		echo "qemu-pxe: BIOS via modern ${lk}" >&2
		timeout "${TIMEOUT}s" "${QEMU}" -machine q35,accel=kvm:tcg -cpu max -m 512 \
			-nographic \
			-kernel "${lk}" \
			-netdev "user,id=n0,tftp=${TFTP}" \
			-device e1000,netdev=n0 \
			>"${LOG}" 2>&1 || true
	else
		echo "qemu-pxe: BIOS via stock QEMU iPXE ROM (HTTPS may fail)" >&2
		timeout "${TIMEOUT}s" "${QEMU}" -machine q35,accel=kvm:tcg -cpu max -m 512 \
			-nographic \
			-boot order=n,once=n \
			-netdev "user,id=n0,tftp=${TFTP},bootfile=metal.ipxe" \
			-device e1000,netdev=n0 \
			>"${LOG}" 2>&1 || true
	fi
}

run_uefi() {
	local efi="${NBP_DIR}/ipxe.efi"
	[[ -f "${OVMF}" ]] || {
		echo "qemu-pxe: OVMF missing at ${OVMF}" >&2
		exit 1
	}
	if [[ "${STOCK_ROM}" -eq 0 && -f "${efi}" ]]; then
		cp -f "${efi}" "${TFTP}/ipxe.efi"
		echo "qemu-pxe: UEFI via modern ${efi} → metal.ipxe → CDN" >&2
		timeout "${TIMEOUT}s" "${QEMU}" -machine q35,accel=kvm:tcg -cpu max -m 512 \
			-nographic \
			-drive if=pflash,format=raw,readonly=on,file="${OVMF}" \
			-boot order=n,once=n \
			-netdev "user,id=n0,tftp=${TFTP},bootfile=ipxe.efi" \
			-device e1000,netdev=n0 \
			>"${LOG}" 2>&1 || true
	else
		local lead_efi="${PROBE_URL}/artifacts/lead/pymergetic.metal.arch.x86_64.efi"
		echo "qemu-pxe: UEFI NBP ← ${lead_efi} (no modern ipxe.efi; skips metal.ipxe)" >&2
		curl -fsSL --connect-timeout 30 -o "${TFTP}/lead.efi" "${lead_efi}"
		timeout "${TIMEOUT}s" "${QEMU}" -machine q35,accel=kvm:tcg -cpu max -m 512 \
			-nographic \
			-drive if=pflash,format=raw,readonly=on,file="${OVMF}" \
			-boot order=n,once=n \
			-netdev "user,id=n0,tftp=${TFTP},bootfile=lead.efi" \
			-device e1000,netdev=n0 \
			>"${LOG}" 2>&1 || true
	fi
}

rm -f "${LOG}"
case "${MODE}" in
bios) run_bios ;;
uefi) run_uefi ;;
*)
	echo "bad mode ${MODE}" >&2
	exit 2
	;;
esac

echo "----- serial (trimmed) -----" >&2
if [[ -f "${LOG}" ]]; then
	tr -d '\r' <"${LOG}" | grep -E \
		'MetalPython|metal |iPXE|platform=|arch=|cdn=|image |X86_64|MicroPython|repl|Filename:|http://|https://|OCSP|failed|FAIL|Invalid|Operation' \
		| tail -80 || true
else
	echo "(no serial log)" >&2
fi

tag="$(echo "${MODE}" | tr '[:lower:]' '[:upper:]')"
if [[ -f "${LOG}" ]] && tr -d '\r' <"${LOG}" | grep -Eq 'metal X86_64|Metal Python|console ok|floor ok'; then
	echo "PXE_QEMU_${tag}_OK CDN=${CDN_URL}" >&2
	exit 0
fi
echo "PXE_QEMU_${tag}_FAIL CDN=${CDN_URL}" >&2
echo "----- serial tail -----" >&2
tail -c 3500 "${LOG}" 2>/dev/null | tr -d '\r' || true
exit 1
