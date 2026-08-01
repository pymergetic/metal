#!/usr/bin/env bash
# Vendors Dropbear into packages/metal/external/dropbear. Prefer Metal glue
# over patches; apply patches/dropbear/*.patch when present.
set -euo pipefail

# scripts/setup.d/deps -> package root (three levels up).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DIR="${ROOT}/external/dropbear"
# DROPBEAR_2024.85 is a known stable tag; override with PM_METAL_DROPBEAR_REF.
REF="${PM_METAL_DROPBEAR_REF:-DROPBEAR_2024.85}"
URL="${PM_METAL_DROPBEAR_URL:-https://github.com/mkj/dropbear.git}"

if [[ -d "${DIR}/.git" ]]; then
	echo "external/dropbear already present"
else
	mkdir -p "${ROOT}/external"
	git clone --depth 1 --branch "${REF}" "${URL}" "${DIR}" || \
		git clone --depth 1 "${URL}" "${DIR}"
	if [[ -d "${DIR}/.git" ]]; then
		git -C "${DIR}" fetch --depth 1 origin "refs/tags/${REF}:refs/tags/${REF}" 2>/dev/null || true
		git -C "${DIR}" checkout "${REF}" 2>/dev/null || true
	fi
fi

shopt -s nullglob
for patch in "${ROOT}"/patches/dropbear/*.patch; do
	echo "dropbear patch: $(basename "${patch}")"
	if git -C "${DIR}" apply --check --reverse "${patch}" >/dev/null 2>&1; then
		echo "  already applied"
		continue
	fi
	if git -C "${DIR}" apply --check "${patch}" >/dev/null 2>&1; then
		git -C "${DIR}" apply "${patch}"
	else
		patch -d "${DIR}" -p1 --forward --batch <"${patch}" || true
	fi
done
shopt -u nullglob

echo "external/dropbear -> ${REF}"
