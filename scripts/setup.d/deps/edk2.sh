#!/usr/bin/env bash
# Vendor Tianocore EDK2 + host nasm into external/edk2 and .tools/nasm.
# Pin: edk2-stable202502 (docs/EFI.md Slice C).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
EDK2_DIR="${ROOT}/external/edk2"
EDK2_REPO="https://github.com/tianocore/edk2.git"
EDK2_REF="edk2-stable202502"
NASM_VER="2.16.03"
NASM_PREFIX="${ROOT}/.tools/nasm"
NASM_BIN="${NASM_PREFIX}/bin/nasm"

mkdir -p "${ROOT}/.tools/src"

if [[ ! -x "${NASM_BIN}" ]]; then
	echo "=== nasm ${NASM_VER} (local, no sudo) ==="
	src="${ROOT}/.tools/src/nasm-${NASM_VER}"
	tarball="${ROOT}/.tools/src/nasm.tar.xz"
	if [[ ! -d "${src}" ]]; then
		curl -fsSL -o "${tarball}" \
			"https://www.nasm.us/pub/nasm/releasebuilds/${NASM_VER}/nasm-${NASM_VER}.tar.xz"
		tar -xJf "${tarball}" -C "${ROOT}/.tools/src"
	fi
	(
		cd "${src}"
		./configure --prefix="${NASM_PREFIX}"
		make -j"$(nproc)"
		make install
	)
fi
echo "nasm: $("${NASM_BIN}" -v)"

if [[ ! -d "${EDK2_DIR}/.git" ]]; then
	echo "=== edk2 ${EDK2_REF} ==="
	git clone --depth 1 --branch "${EDK2_REF}" "${EDK2_REPO}" "${EDK2_DIR}"
fi

git -C "${EDK2_DIR}" fetch --tags --depth 1 origin "${EDK2_REF}"
git -C "${EDK2_DIR}" checkout --force "FETCH_HEAD" 2>/dev/null \
	|| git -C "${EDK2_DIR}" checkout --force "${EDK2_REF}"
git -C "${EDK2_DIR}" submodule update --init --depth 1 --recursive

export PATH="${NASM_PREFIX}/bin:${PATH}"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"

# Build BaseTools once.
if [[ ! -x "${EDK2_DIR}/BaseTools/Source/C/bin/GenFw" ]]; then
	echo "=== EDK2 BaseTools ==="
	make -C "${EDK2_DIR}/BaseTools" -j"$(nproc)"
fi

echo "external/edk2 -> ${EDK2_REF} ($("git" -C "${EDK2_DIR}" rev-parse --short HEAD))"
echo "edk2 setup: ok"
