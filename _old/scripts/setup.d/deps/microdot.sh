#!/usr/bin/env bash
# Vendors Miguel Grinberg's Microdot into external/microdot (gitignored).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DIR="${ROOT}/external/microdot"
REPO="${PM_METAL_MICRODOT_URL:-https://github.com/miguelgrinberg/microdot.git}"
REF="${PM_METAL_MICRODOT_REF:-v2.6.2}"

if [[ ! -d "${DIR}/.git" ]]; then
	git clone "${REPO}" "${DIR}"
fi

git -C "${DIR}" fetch --tags origin
git -C "${DIR}" checkout --force "${REF}"
git -C "${DIR}" clean -x -f -d

shopt -s nullglob
for patch in "${ROOT}"/patches/microdot/*.patch; do
	echo "microdot patch: $(basename "${patch}")"
	git -C "${DIR}" apply --whitespace=nowarn "${patch}"
done
shopt -u nullglob

echo "external/microdot -> ${REF}"
