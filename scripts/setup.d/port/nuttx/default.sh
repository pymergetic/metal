#!/usr/bin/env bash
# Vendor NuttX + apps into external/ (gitignored) and register Metal as
# apps/system/pm_metal. See src/nuttx/README.md, docs/SOURCETREE.md.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
NUTTX_DIR="${ROOT}/external/nuttx"
APPS_DIR="${ROOT}/external/nuttx-apps"
NUTTX_REPO="${NUTTX_REPO:-https://github.com/apache/nuttx.git}"
APPS_REPO="${APPS_REPO:-https://github.com/apache/nuttx-apps.git}"
# Pin can be bumped deliberately; keep in sync with README if changed.
NUTTX_REF="${NUTTX_REF:-master}"

if [[ -f "${ROOT}/.venv/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${ROOT}/.venv/bin/activate"
fi

# NuttX CMake configure needs kconfiglib (not always present in a fresh venv).
if ! python3 -c "import kconfiglib" 2>/dev/null; then
	pip install kconfiglib
fi

"${ROOT}/scripts/setup.d/deps/wamr.sh"
"${ROOT}/scripts/setup.d/deps/lz4.sh"
"${ROOT}/scripts/setup.d/deps/microtar.sh"

if [[ ! -d "${NUTTX_DIR}/.git" ]]; then
	git clone "${NUTTX_REPO}" "${NUTTX_DIR}"
fi
if [[ ! -d "${APPS_DIR}/.git" ]]; then
	git clone "${APPS_REPO}" "${APPS_DIR}"
fi

git -C "${NUTTX_DIR}" fetch --tags origin
git -C "${APPS_DIR}" fetch --tags origin
git -C "${NUTTX_DIR}" checkout --force "${NUTTX_REF}"
git -C "${APPS_DIR}" checkout --force "${NUTTX_REF}"

mkdir -p "${APPS_DIR}/system"
ln -sfn "${ROOT}/src/nuttx" "${APPS_DIR}/system/pm_metal"

# Local wamr/ name expected by upstream product-mini wamr.mk if ever used.
ln -sfn "${ROOT}/external/wamr" "${ROOT}/src/nuttx/wamr"

echo "nuttx ready"
echo "  NUTTX_DIR=${NUTTX_DIR}"
echo "  APPS_DIR=${APPS_DIR}"
echo "  app symlink: ${APPS_DIR}/system/pm_metal -> ${ROOT}/src/nuttx"
