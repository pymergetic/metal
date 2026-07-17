#!/usr/bin/env bash
# Vendors libcurl into external/curl (gitignored — see
# docs/SOURCETREE.md "Vendoring") — HTTP/1.1 + HTTP/2 client; TLS via
# vendored mbedTLS (see setup-mbedtls.sh), h2 via nghttp2.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="${ROOT}/external/curl"
REPO="https://github.com/curl/curl"
REF="curl-8_12_1"

if [ ! -d "${DIR}/.git" ]; then
	git clone "${REPO}" "${DIR}"
fi

git -C "${DIR}" fetch --tags origin
git -C "${DIR}" checkout --force "${REF}"
git -C "${DIR}" clean -x -f -d

for patch in "${ROOT}"/patches/curl/*.patch; do
	[ -f "${patch}" ] || continue
	echo "curl patch: $(basename "${patch}")"
	git -C "${DIR}" apply --whitespace=nowarn "${patch}"
done

echo "external/curl -> ${REF} + $(ls "${ROOT}/patches/curl"/*.patch 2>/dev/null | wc -l) patch(es)"
