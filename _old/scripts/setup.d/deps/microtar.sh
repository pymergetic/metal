#!/usr/bin/env bash
# Vendors microtar into external/microtar (gitignored — see docs/SOURCETREE.md
# "Vendoring") — same reproducible pin+checkout shape as setup-lz4.sh.
# external/microtar itself stays a plain, reproducible upstream checkout —
# nothing here is ever hand-edited in place; re-run this script any time
# to get back to (pin + patches), including after `rm -rf external/microtar`.
#
# Pinned to a raw commit, not a tag: upstream's own last tag (v0.1.0) is one
# commit behind its last-ever commit (added C++ `extern "C"` support, harmless
# and worth having); the repo has had no activity since 2017, so HEAD and this
# pin are the same thing today — pinning the commit directly documents that
# explicitly instead of implying a newer tag might one day exist.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
MICROTAR_DIR="${ROOT}/external/microtar"
MICROTAR_REPO="https://github.com/rxi/microtar"
MICROTAR_REF="27076e1b9290e9c7842bb7890a54fcf172406c84"

if [ ! -d "${MICROTAR_DIR}/.git" ]; then
	git clone "${MICROTAR_REPO}" "${MICROTAR_DIR}"
fi

git -C "${MICROTAR_DIR}" fetch --tags origin
git -C "${MICROTAR_DIR}" checkout --force "${MICROTAR_REF}"
# -x also removes anything a previous run of this same script (or a stray
# local build) might have left behind — -d for untracked directories, -f
# is redundant with -x but spelled out for clarity.
git -C "${MICROTAR_DIR}" clean -x -f -d

shopt -s nullglob
for patch in "${ROOT}"/patches/microtar/*.patch; do
	echo "microtar patch: $(basename "${patch}")"
	git -C "${MICROTAR_DIR}" apply --whitespace=nowarn "${patch}"
done
shopt -u nullglob

echo "external/microtar -> ${MICROTAR_REF}"
