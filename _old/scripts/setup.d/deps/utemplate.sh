#!/usr/bin/env bash
# Vendors utemplate into external/utemplate (gitignored).
# Prefer the copy bundled with Microdot (libs/common/utemplate); requires
# ./_old/scripts/setup microdot first. Hand-bumped with that tree.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DIR="${ROOT}/external/utemplate"
SRC="${ROOT}/external/microdot/libs/common/utemplate"

if [[ ! -d "${SRC}" ]]; then
	echo "utemplate: missing ${SRC} - run ./_old/scripts/setup microdot first" >&2
	exit 1
fi

rm -rf "${DIR}"
mkdir -p "${DIR}"
cp -a "${SRC}/." "${DIR}/"
# Drop host bytecode leftovers if any.
find "${DIR}" -type d -name '__pycache__' -exec rm -rf {} + 2>/dev/null || true

echo "external/utemplate <- microdot/libs/common/utemplate"
