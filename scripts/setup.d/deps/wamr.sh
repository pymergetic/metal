#!/usr/bin/env bash
# Vendors WAMR into external/wamr (gitignored — see docs/SOURCETREE.md
# "Vendoring"). Optional patches/wamr/*.patch if present (archive may
# carry them; freestanding-efi has none).
# external/wamr itself stays a plain, reproducible upstream checkout —
# nothing here is ever hand-edited in place; re-run this script any time
# to get back to (pin + optional patches), including after `rm -rf external/wamr`.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
WAMR_DIR="${ROOT}/external/wamr"
WAMR_REPO="https://github.com/bytecodealliance/wasm-micro-runtime"
WAMR_REF="WAMR-2.4.5"

if [ ! -d "${WAMR_DIR}/.git" ]; then
	git clone "${WAMR_REPO}" "${WAMR_DIR}"
fi

git -C "${WAMR_DIR}" fetch --tags origin
git -C "${WAMR_DIR}" checkout --force "${WAMR_REF}"
git -C "${WAMR_DIR}" clean -x -f -d

shopt -s nullglob
for patch in "${ROOT}"/patches/wamr/*.patch; do
	echo "wamr patch: $(basename "${patch}")"
	git -C "${WAMR_DIR}" apply --whitespace=nowarn "${patch}"
done
shopt -u nullglob

echo "external/wamr -> ${WAMR_REF}"
