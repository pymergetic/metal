#!/usr/bin/env bash
# Vendors mbedTLS into external/mbedtls (gitignored — see
# docs/SOURCETREE.md "Vendoring") — same pin+checkout shape as setup-lz4.sh.
# LTS 3.6.x — curl requires >= 3.2.0.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DIR="${ROOT}/external/mbedtls"
REPO="https://github.com/Mbed-TLS/mbedtls"
REF="v3.6.3"

if [ ! -d "${DIR}/.git" ]; then
	git clone "${REPO}" "${DIR}"
fi

git -C "${DIR}" fetch --tags origin
git -C "${DIR}" checkout --force "${REF}"
git -C "${DIR}" clean -x -f -d

# mbedTLS 3.x ships mbedtls-framework / everest / p256-m as submodules.
git -C "${DIR}" submodule update --init --recursive --depth 1

for patch in "${ROOT}"/patches/mbedtls/*.patch; do
	[ -f "${patch}" ] || continue
	echo "mbedtls patch: $(basename "${patch}")"
	git -C "${DIR}" apply --whitespace=nowarn "${patch}"
done

echo "external/mbedtls -> ${REF} + $(ls "${ROOT}/patches/mbedtls"/*.patch 2>/dev/null | wc -l) patch(es)"
