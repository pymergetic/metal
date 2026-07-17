#!/usr/bin/env bash
# Vendors Monocypher into external/monocypher (gitignored — see
# docs/SOURCETREE.md "Vendoring") — same pin+checkout shape as setup-lz4.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="${ROOT}/external/monocypher"
REPO="https://github.com/LoupVaillant/Monocypher"
REF="4.0.2"

if [ ! -d "${DIR}/.git" ]; then
	git clone "${REPO}" "${DIR}"
fi

git -C "${DIR}" fetch --tags origin
git -C "${DIR}" checkout --force "${REF}"
git -C "${DIR}" clean -x -f -d

for patch in "${ROOT}"/patches/monocypher/*.patch; do
	[ -f "${patch}" ] || continue
	echo "monocypher patch: $(basename "${patch}")"
	git -C "${DIR}" apply --whitespace=nowarn "${patch}"
done

echo "external/monocypher -> ${REF} + $(ls "${ROOT}/patches/monocypher"/*.patch 2>/dev/null | wc -l) patch(es)"
