#!/usr/bin/env bash
# Vendors vanilla MicroPython into external/micropython (gitignored —
# pin + patches/micropython/ only; see docs/SOURCETREE.md "Vendoring").
# No forks. Metal glue lives under src/pymergetic/metal/py/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
UPY_DIR="${ROOT}/external/micropython"
UPY_REPO="https://github.com/micropython/micropython"
UPY_REF="v1.28.0"

if [ ! -d "${UPY_DIR}/.git" ]; then
	git clone --depth 1 --branch "${UPY_REF}" "${UPY_REPO}" "${UPY_DIR}" || {
		git clone "${UPY_REPO}" "${UPY_DIR}"
		git -C "${UPY_DIR}" fetch --tags origin
		git -C "${UPY_DIR}" checkout --force "${UPY_REF}"
	}
else
	git -C "${UPY_DIR}" fetch --tags origin
	git -C "${UPY_DIR}" checkout --force "${UPY_REF}"
fi
git -C "${UPY_DIR}" clean -x -f -d

# Submodules needed for some builds; lib/berkeley-db etc. optional for trim.
if [ -f "${UPY_DIR}/.gitmodules" ]; then
	git -C "${UPY_DIR}" submodule update --init --depth 1 lib/micropython-lib 2>/dev/null || true
fi

shopt -s nullglob
for patch in "${ROOT}"/patches/micropython/*.patch; do
	echo "micropython patch: $(basename "${patch}")"
	git -C "${UPY_DIR}" apply --whitespace=nowarn "${patch}"
done
shopt -u nullglob

echo "external/micropython -> ${UPY_REF}"

# Generate port-neutral embed package for Metal link (EFI/BIOS).
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/micropython.sh"
ROOT="${ROOT}" pm_metal_upy_generate_embed
