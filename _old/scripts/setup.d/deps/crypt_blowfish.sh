#!/usr/bin/env bash
# Vendors Openwall crypt_blowfish into external/crypt_blowfish (gitignored).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DIR="${ROOT}/external/crypt_blowfish"
# Upstream is a single-branch repo (no "master"); optional REF pins a commit/tag.
REF="${PM_METAL_CRYPT_BLOWFISH_REF:-}"
URL="${PM_METAL_CRYPT_BLOWFISH_URL:-https://github.com/openwall/crypt_blowfish.git}"

if [[ -d "${DIR}/.git" ]]; then
	echo "external/crypt_blowfish already present"
	exit 0
fi

mkdir -p "${ROOT}/external"
if [[ -n "${REF}" ]]; then
	git clone --depth 1 --branch "${REF}" "${URL}" "${DIR}"
	echo "external/crypt_blowfish -> ${REF}"
else
	git clone --depth 1 "${URL}" "${DIR}"
	echo "external/crypt_blowfish -> $(git -C "${DIR}" rev-parse --short HEAD)"
fi
