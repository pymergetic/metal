#!/usr/bin/env bash
# Build modern iPXE NBPs with HTTPS (BIOS undionly + UEFI ipxe.efi + qemu lkrn).
# Ubuntu's packaged iPXE (2022) fails TLS against current CDNs; use these instead.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${ROOT}/nbp"
SRC="${IPXE_SRC:-/tmp/ipxe-build}"
JOBS="$(nproc 2>/dev/null || echo 4)"

need_lzma() {
	if [[ -f /usr/include/lzma.h ]]; then
		return 0
	fi
	local debdir="${SRC}/.lzma-deb"
	if [[ ! -f "${debdir}/usr/include/lzma.h" ]]; then
		mkdir -p "${debdir}"
		(cd /tmp && apt-get download liblzma5 liblzma-dev >/dev/null)
		dpkg-deb -x /tmp/liblzma5_*.deb "${debdir}"
		dpkg-deb -x /tmp/liblzma-dev_*.deb "${debdir}"
	fi
	export C_INCLUDE_PATH="${debdir}/usr/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
	export LIBRARY_PATH="${debdir}/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
	export LD_LIBRARY_PATH="${debdir}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
	HOST_CFLAGS_EXTRA="-I${debdir}/usr/include"
	ZBIN_LDFLAGS_EXTRA="-L${debdir}/usr/lib/x86_64-linux-gnu -llzma"
}

if [[ ! -d "${SRC}/.git" ]]; then
	git clone --depth 1 https://github.com/ipxe/ipxe.git "${SRC}"
fi

need_lzma
HOST_CFLAGS_EXTRA="${HOST_CFLAGS_EXTRA:-}"
ZBIN_LDFLAGS_EXTRA="${ZBIN_LDFLAGS_EXTRA:--llzma}"

# pcbios defaults undef HTTPS — force it back on for CDN bootstrap.
mkdir -p "${SRC}/src/config/local"
cat >"${SRC}/src/config/local/general.h" <<'EOF'
/* Metal CDN HTTPS PXE — must override pcbios #undef DOWNLOAD_PROTO_HTTPS */
#undef DOWNLOAD_PROTO_HTTPS
#define DOWNLOAD_PROTO_HTTPS
EOF

cat >/tmp/embed-metal-nbp.ipxe <<'EOF'
#!ipxe
dhcp
chain --autofree tftp://${next-server}/metal.ipxe || shell
EOF

make -C "${SRC}/src" -j"${JOBS}" \
	HOST_CFLAGS="-O2 ${HOST_CFLAGS_EXTRA}" \
	ZBIN_LDFLAGS="${ZBIN_LDFLAGS_EXTRA}" \
	bin/undionly.kpxe bin/ipxe.lkrn \
	bin-x86_64-efi/ipxe.efi \
	EMBED=/tmp/embed-metal-nbp.ipxe

mkdir -p "${OUT}"
cp -f "${SRC}/src/bin/undionly.kpxe" \
	"${SRC}/src/bin/ipxe.lkrn" \
	"${SRC}/src/bin-x86_64-efi/ipxe.efi" \
	"${OUT}/"
echo "OK → ${OUT}/"
ls -la "${OUT}/"

# Sync downloadable NBPs into metal-cdn UI static tree when present.
# deploy/bootserver → deploy → metal → extmod → metalpython → packages → metal-cdn/…
CDN_STATIC="${ROOT}/../../../../../metal-cdn/pymergetic/metal/cdn/web/static/netboot"
if [[ -d "$(dirname "${CDN_STATIC}")" ]]; then
	mkdir -p "${CDN_STATIC}"
	cp -f "${OUT}/undionly.kpxe" "${OUT}/ipxe.efi" "${CDN_STATIC}/"
	TPL="${ROOT}/metal.ipxe.tpl"
	if [[ -f "${TPL}" ]]; then
		sed \
			-e 's|__NEXT_SERVER__|192.168.10.1|g' \
			-e 's|__IMAGE_URL__|https://cdn.pymergetic.com/cdn/artifacts/lead/pymergetic.metal.arch.x86_64.elf|g' \
			-e 's|__IMAGE_EFI_URL__|https://cdn.pymergetic.com/cdn/artifacts/lead/pymergetic.metal.arch.x86_64.efi|g' \
			-e 's|__CDN_URL__|https://cdn.pymergetic.com/cdn|g' \
			"${TPL}" >"${CDN_STATIC}/metal.ipxe"
	fi
	echo "OK → ${CDN_STATIC}/ (CDN UI Netboot downloads)"
fi
