#!/usr/bin/env bash
# Vendors LZ4 into external/lz4 (gitignored — see docs/SOURCETREE.md
# "Vendoring") — same reproducible pin+checkout shape as setup-wamr.sh.
# external/lz4 itself stays a plain, reproducible upstream checkout —
# nothing here is ever hand-edited in place; re-run this script any time
# to get back to (pin + patches), including after `rm -rf external/lz4`.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
LZ4_DIR="${ROOT}/external/lz4"
LZ4_REPO="https://github.com/lz4/lz4"
LZ4_REF="v1.10.0"

if [ ! -d "${LZ4_DIR}/.git" ]; then
	git clone "${LZ4_REPO}" "${LZ4_DIR}"
fi

git -C "${LZ4_DIR}" fetch --tags origin
git -C "${LZ4_DIR}" checkout --force "${LZ4_REF}"
# -x also removes anything a previous run of this same script (or a stray
# local build) might have left behind — -d for untracked directories, -f
# is redundant with -x but spelled out for clarity.
git -C "${LZ4_DIR}" clean -x -f -d

shopt -s nullglob
for patch in "${ROOT}"/patches/lz4/*.patch; do
	echo "lz4 patch: $(basename "${patch}")"
	git -C "${LZ4_DIR}" apply --whitespace=nowarn "${patch}"
done
shopt -u nullglob

echo "external/lz4 -> ${LZ4_REF}"
