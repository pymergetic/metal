#!/usr/bin/env bash
# Vendors nghttp2 into external/nghttp2 (gitignored — see
# docs/SOURCETREE.md "Vendoring") — HTTP/2 for libcurl.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DIR="${ROOT}/external/nghttp2"
REPO="https://github.com/nghttp2/nghttp2"
REF="v1.65.0"

if [ ! -d "${DIR}/.git" ]; then
	git clone "${REPO}" "${DIR}"
fi

git -C "${DIR}" fetch --tags origin
git -C "${DIR}" checkout --force "${REF}"
git -C "${DIR}" clean -x -f -d

# nghttp2 uses a submodule for neverbleed / mruby only when building apps;
# lib-only does not need them. Keep checkout clean.

for patch in "${ROOT}"/patches/nghttp2/*.patch; do
	[ -f "${patch}" ] || continue
	echo "nghttp2 patch: $(basename "${patch}")"
	git -C "${DIR}" apply --whitespace=nowarn "${patch}"
done

echo "external/nghttp2 -> ${REF} + $(ls "${ROOT}/patches/nghttp2"/*.patch 2>/dev/null | wc -l) patch(es)"
