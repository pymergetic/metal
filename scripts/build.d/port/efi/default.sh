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

# MicroPython embed package (port-neutral sources under build/micropython_embed).
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/micropython.sh"
pm_metal_upy_generate_embed
# Refresh Metal.inf µPy source list (BEGIN/END markers).
ROOT="${ROOT}" python3 - <<'PY'
from pathlib import Path
import os
root = Path(os.environ["ROOT"])
inf = root / "src/efi/MetalPkg/Metal.inf"
embed = root / "build/micropython_embed"
inf_dir = inf.parent
lines = inf.read_text().splitlines(keepends=True)
out = []
i = 0
while i < len(lines):
    if lines[i].strip() == "# BEGIN_MICROPYTHON":
        out.append(lines[i] if lines[i].endswith("\n") else lines[i] + "\n")
        i += 1
        while i < len(lines) and lines[i].strip() != "# END_MICROPYTHON":
            i += 1
        glue = [
            "src/pymergetic/metal/py/mphalport_metal.c",
            "src/pymergetic/metal/py/py.c",
            "src/pymergetic/metal/py/py_bind.c",
            "src/pymergetic/metal/py/py_obj.c",
            "src/pymergetic/metal/py/py_await.c",
            "src/pymergetic/metal/py/py_shell.c",
            "src/pymergetic/metal/py/py_zip.c",
            "src/pymergetic/metal/py/py_guest.c",
            "src/pymergetic/metal/py/py_port_stubs.c",
        ]
        for g in glue:
            out.append(f"  {os.path.relpath(root / g, inf_dir)}\n")
        skip = {
            "mphalport.c",
            "asmarm.c", "asmrv32.c", "asmthumb.c", "asmxtensa.c", "asmx86.c",
            "emitnarm.c", "emitnrv32.c", "emitnthumb.c", "emitnxtensa.c",
            "emitnxtensawin.c", "emitnx86.c", "emitinlinethumb.c",
            "emitinlinextensa.c",
            "nlraarch64.c", "nlrmips.c", "nlrpowerpc.c", "nlrrv32.c",
            "nlrrv64.c", "nlrthumb.c", "nlrxtensa.c", "nlrx86.c",
        }
        for p in sorted(embed.glob("py/*.c")):
            if p.name in skip:
                continue
            out.append(f"  {os.path.relpath(p, inf_dir)}\n")
        for p in sorted((embed / "shared").rglob("*.c")):
            out.append(f"  {os.path.relpath(p, inf_dir)}\n")
        for p in sorted((embed / "port").glob("*.c")):
            if p.name in skip:
                continue
            out.append(f"  {os.path.relpath(p, inf_dir)}\n")
        if i < len(lines):
            out.append(lines[i] if lines[i].endswith("\n") else lines[i] + "\n")
            i += 1
        continue
    out.append(lines[i])
    i += 1
inf.write_text("".join(out))
PY

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
