#!/usr/bin/env bash
# Vendors lwIP into external/lwip (gitignored — see docs/SOURCETREE.md
# "Vendoring"). Optional patches/lwip/*.patch if present.
# No src/efi symlink — Metal.inf / -I point at external/lwip directly.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
LWIP_DIR="${ROOT}/external/lwip"
LWIP_REPO="https://github.com/lwip-tcpip/lwip.git"
LWIP_REF="STABLE-2_2_1_RELEASE"

if [ ! -d "${LWIP_DIR}/.git" ]; then
	git clone "${LWIP_REPO}" "${LWIP_DIR}"
fi

git -C "${LWIP_DIR}" fetch --tags origin
git -C "${LWIP_DIR}" checkout --force "${LWIP_REF}"
git -C "${LWIP_DIR}" clean -x -f -d

shopt -s nullglob
for patch in "${ROOT}"/patches/lwip/*.patch; do
	echo "lwip patch: $(basename "${patch}")"
	git -C "${LWIP_DIR}" apply --whitespace=nowarn "${patch}"
done
shopt -u nullglob

# Drop legacy symlink if present (older setup created src/efi/lwip).
rm -f "${ROOT}/src/efi/lwip"

echo "external/lwip -> ${LWIP_REF}"
