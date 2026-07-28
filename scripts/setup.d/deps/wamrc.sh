#!/usr/bin/env bash
# Fetch prebuilt wamrc (WAMR AOT compiler) matching external/wamr pin.
# Avoids building LLVM on the host. Output: .tools/wamrc/wamrc
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
WAMR_REF="WAMR-2.4.5"
VER="${WAMR_REF#WAMR-}"
OUT_DIR="${ROOT}/.tools/wamrc"
BIN="${OUT_DIR}/wamrc"
ASSET="wamrc-${VER}-x86_64-ubuntu-22.04.tar.gz"
URL="https://github.com/bytecodealliance/wasm-micro-runtime/releases/download/${WAMR_REF}/${ASSET}"

if [[ -x "${BIN}" ]]; then
	echo "wamrc: keep ${BIN}"
	exit 0
fi

mkdir -p "${OUT_DIR}"
echo "wamrc: fetch ${URL}"
curl -fL --retry 3 -o "${OUT_DIR}/${ASSET}" "${URL}"
tar -xzf "${OUT_DIR}/${ASSET}" -C "${OUT_DIR}"
# Tarball may place wamrc at top level or in a subdir.
if [[ ! -x "${BIN}" ]]; then
	found="$(find "${OUT_DIR}" -type f -name wamrc -print -quit)"
	if [[ -n "${found}" && "${found}" != "${BIN}" ]]; then
		mv -f "${found}" "${BIN}"
	fi
fi
chmod +x "${BIN}"
rm -f "${OUT_DIR}/${ASSET}"
"${BIN}" --version 2>/dev/null || true
echo "wamrc: ok -> ${BIN}"
