#!/usr/bin/env bash
# Build Metal.efi via EDK2 → build/efi/metal.efi
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
EDK2="${ROOT}/external/edk2"
NASM_BIN="${ROOT}/.tools/nasm/bin"
OUT_DIR="${ROOT}/build/efi"
TOOL_CHAIN="${PM_METAL_EDK2_TOOL_CHAIN:-GCC}"
TARGET="${PM_METAL_EDK2_TARGET:-RELEASE}"

if [[ ! -d "${EDK2}/.git" ]] || [[ ! -x "${NASM_BIN}/nasm" ]]; then
	echo "efi build: run ./scripts/setup edk2 first" >&2
	exit 1
fi
if [[ ! -x "${EDK2}/BaseTools/Source/C/bin/GenFw" ]]; then
	echo "efi build: BaseTools missing — run ./scripts/setup edk2" >&2
	exit 1
fi

export PATH="${NASM_BIN}:${PATH}"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
export WORKSPACE="${EDK2}"
export PACKAGES_PATH="${EDK2}:${ROOT}/src/efi"
export EDK_TOOLS_PATH="${EDK2}/BaseTools"
export CONF_PATH="${EDK2}/Conf"

mkdir -p "${OUT_DIR}" "${CONF_PATH}"

set +u
# shellcheck disable=SC1091
source "${EDK2}/edksetup.sh" BaseTools
set -u

[[ -f "${CONF_PATH}/target.txt" ]] || cp "${EDK_TOOLS_PATH}/Conf/target.template" "${CONF_PATH}/target.txt"
[[ -f "${CONF_PATH}/tools_def.txt" ]] || cp "${EDK_TOOLS_PATH}/Conf/tools_def.template" "${CONF_PATH}/tools_def.txt"
[[ -f "${CONF_PATH}/build_rule.txt" ]] || cp "${EDK_TOOLS_PATH}/Conf/build_rule.template" "${CONF_PATH}/build_rule.txt"

# Bake CA publics (or stub) for trust.c
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/pki.sh"
pm_metal_pki_bake

# Embed guest wasm (hello / ui_hello / async_sleep) before EDK2 compile.
"${ROOT}/scripts/build.d/port/efi/embed-mods.sh"
# Doom parked. Opt-in: METAL_DOOM_BUILD=1 → build/doom/ (EFI+BIOS/PXE).
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/doom.sh"
if [[ "${METAL_DOOM_BUILD:-0}" == "1" ]]; then
	pm_metal_doom_build
fi

echo "efi build: MetalPkg (X64 ${TOOL_CHAIN} ${TARGET})"
build \
	-p MetalPkg/MetalPkg.dsc \
	-a X64 \
	-t "${TOOL_CHAIN}" \
	-b "${TARGET}" \
	-n "$(nproc)"

EFI_BUILT="${WORKSPACE}/Build/Metal/${TARGET}_${TOOL_CHAIN}/X64/Metal.efi"
if [[ ! -f "${EFI_BUILT}" ]]; then
	echo "efi build: missing ${EFI_BUILT}" >&2
	exit 1
fi

cp -f "${EFI_BUILT}" "${OUT_DIR}/metal.efi"
if pm_metal_pki_want_sign && [[ -f "$(pm_metal_pki_dir)/kernel/ca.key" ]]; then
	"${ROOT}/scripts/pki" sign-elf "${OUT_DIR}/metal.efi" || true
fi
ls -la "${OUT_DIR}/metal.efi"
echo "efi build: ok mode=$(pm_metal_pki_trust_mode) -> ${OUT_DIR}/metal.efi"
