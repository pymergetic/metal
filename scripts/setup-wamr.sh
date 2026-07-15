#!/usr/bin/env bash
# Vendors WAMR into external/wamr (gitignored — see docs/SOURCETREE.md
# "Vendoring") and applies this tree's own patches/wamr/*.patch on top.
# external/wamr itself stays a plain, reproducible upstream checkout —
# nothing here is ever hand-edited in place; re-run this script any time
# to get back to (pin + patches), including after `rm -rf external/wamr`.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WAMR_DIR="${ROOT}/external/wamr"
WAMR_REPO="https://github.com/bytecodealliance/wasm-micro-runtime"
WAMR_REF="WAMR-2.4.5"

if [ ! -d "${WAMR_DIR}/.git" ]; then
	git clone "${WAMR_REPO}" "${WAMR_DIR}"
fi

git -C "${WAMR_DIR}" fetch --tags origin
git -C "${WAMR_DIR}" checkout --force "${WAMR_REF}"
# -x also removes anything patches below might have left behind on a
# previous run of this same script (e.g. re-running after editing a
# patch) — -d for untracked directories, -f is redundant with -x but
# spelled out for clarity.
git -C "${WAMR_DIR}" clean -x -f -d

for patch in "${ROOT}"/patches/wamr/*.patch; do
	[ -f "${patch}" ] || continue
	echo "wamr patch: $(basename "${patch}")"
	git -C "${WAMR_DIR}" apply --whitespace=nowarn "${patch}"
done

echo "external/wamr -> ${WAMR_REF} + $(ls "${ROOT}/patches/wamr"/*.patch 2>/dev/null | wc -l) patch(es)"
