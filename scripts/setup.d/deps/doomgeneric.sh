#!/usr/bin/env bash
# Vendors ozkl/doomgeneric into external/doomgeneric (gitignored — see
# docs/SOURCETREE.md "Vendoring"). Plain upstream checkout only — Metal
# adaptations stay in mods/apps/doom (+ --wrap), never in this tree.
# Re-run anytime (or after rm -rf external/doomgeneric) to restore vanilla.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DIR="${ROOT}/external/doomgeneric"
REPO="https://github.com/ozkl/doomgeneric.git"
# Tip of upstream master as of Metal doom bring-up ("boolean fix").
REF="dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284"
WAD_CACHE="${ROOT}/.tools/doom/doom1.wad"

if [ ! -d "${DIR}/.git" ]; then
	git clone "${REPO}" "${DIR}"
fi

git -C "${DIR}" fetch origin
git -C "${DIR}" checkout --force "${REF}"
git -C "${DIR}" clean -x -f -d

# No patches/doomgeneric — keep external vanilla. Guest glue is mods/apps/doom.

if [[ ! -f "${WAD_CACHE}" ]]; then
	mkdir -p "$(dirname "${WAD_CACHE}")"
	echo "doomgeneric: fetching shareware IWAD -> ${WAD_CACHE}"
	curl -fsSL -o "${WAD_CACHE}" \
		"https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad"
fi

echo "external/doomgeneric -> ${REF}"
