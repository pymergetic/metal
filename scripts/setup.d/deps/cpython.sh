#!/usr/bin/env bash
# Vendors CPython into external/cpython (gitignored — see docs/SOURCETREE.md
# "Vendoring") — same reproducible pin+checkout shape as setup-wamr.sh.
# Pin matches packages/kernel/third_party (v3.14.6). external/cpython itself
# stays a plain upstream checkout — nothing here is ever hand-edited in place;
# re-run this script any time to get back to (pin + patches), including after
# `rm -rf external/cpython`.
#
# Guest build (wasm32-wasip1-threads): scripts/build.d/guest/cpython.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CPYTHON_DIR="${ROOT}/external/cpython"
CPYTHON_REPO="https://github.com/python/cpython"
CPYTHON_REF="v3.14.6"

if [ ! -d "${CPYTHON_DIR}/.git" ]; then
	git clone "${CPYTHON_REPO}" "${CPYTHON_DIR}"
fi

git -C "${CPYTHON_DIR}" fetch --tags origin
git -C "${CPYTHON_DIR}" checkout --force "${CPYTHON_REF}"
# -x also removes anything a previous run of this same script (or a stray
# local / cross-build tree) might have left behind — -d for untracked
# directories, -f is redundant with -x but spelled out for clarity.
git -C "${CPYTHON_DIR}" clean -x -f -d

for patch in "${ROOT}"/patches/cpython/*.patch; do
	[ -f "${patch}" ] || continue
	echo "cpython patch: $(basename "${patch}")"
	git -C "${CPYTHON_DIR}" apply --whitespace=nowarn "${patch}"
done

echo "external/cpython -> ${CPYTHON_REF} + $(ls "${ROOT}/patches/cpython"/*.patch 2>/dev/null | wc -l) patch(es)"
