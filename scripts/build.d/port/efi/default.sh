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

# Regenerate typings/**.pyi from PM_METAL_PY_BIND / PM_METAL_SHELL_CMD(S) /
# pm_metal_mod_register_func call sites (Phase 2e) — editor/linter support
# only, does not affect the firmware build itself.
python3 "${ROOT}/scripts/gen_py_stubs.py" || true

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
            "src/pymergetic/metal/py/py_ctx.c",
            "src/pymergetic/metal/py/py_obj.c",
            "src/pymergetic/metal/py/py_await.c",
            "src/pymergetic/metal/py/py_shell.c",
            "src/pymergetic/metal/py/py_zip.c",
            "src/pymergetic/metal/py/py_zip_embed.c",
            "src/pymergetic/metal/py/py_zip_read.c",
            "src/pymergetic/metal/py/py_guest.c",
            "src/pymergetic/metal/py/py_port_stubs.c",
                "src/pymergetic/metal/fs/fs_py_bind.c",
                "src/pymergetic/metal/dev/random/random_py_bind.c",
                "src/pymergetic/metal/dev/random/time_py_bind.c",
                "src/pymergetic/metal/dev/audio/audio_py_bind.c",
                "src/pymergetic/metal/util/tar_py_bind.c",
                "src/pymergetic/metal/net/tls/tls_conn.c",
                "src/pymergetic/metal/net/tls/tls_py_bind.c",
                "src/pymergetic/metal/net/ip/ip_py_bind.c",
                "src/pymergetic/metal/net/http/http_py_bind.c",
                "external/micropython/extmod/modbinascii.c",
                "external/micropython/extmod/modrandom.c",
                "external/micropython/extmod/modhashlib.c",
                "external/micropython/extmod/modre.c",
                "external/micropython/extmod/moddeflate.c",
                "external/micropython/extmod/modjson.c",
                # Freestanding single-precision libm (µPy SRC_LIB_LIBM_C shape).
                # sqrtf/floorf/ceilf/truncf: WAMR. math.c via py_libm_math.c
                # (renames those three). Skip ef_sqrt / thumb_vfp_sqrtf.
                "src/pymergetic/metal/py/py_libm_extra.c",
                "src/pymergetic/metal/py/py_libm_math.c",
                "external/micropython/lib/libm/acoshf.c",
                "external/micropython/lib/libm/asinfacosf.c",
                "external/micropython/lib/libm/asinhf.c",
                "external/micropython/lib/libm/atan2f.c",
                "external/micropython/lib/libm/atanf.c",
                "external/micropython/lib/libm/atanhf.c",
                "external/micropython/lib/libm/ef_rem_pio2.c",
                "external/micropython/lib/libm/erf_lgamma.c",
                "external/micropython/lib/libm/fmodf.c",
                "external/micropython/lib/libm/kf_cos.c",
                "external/micropython/lib/libm/kf_rem_pio2.c",
                "external/micropython/lib/libm/kf_sin.c",
                "external/micropython/lib/libm/kf_tan.c",
                "external/micropython/lib/libm/log1pf.c",
                "external/micropython/lib/libm/nearbyintf.c",
                "external/micropython/lib/libm/roundf.c",
                "external/micropython/lib/libm/sf_cos.c",
                "external/micropython/lib/libm/sf_erf.c",
                "external/micropython/lib/libm/sf_frexp.c",
                "external/micropython/lib/libm/sf_ldexp.c",
                "external/micropython/lib/libm/sf_modf.c",
                "external/micropython/lib/libm/sf_sin.c",
                "external/micropython/lib/libm/sf_tan.c",
                "external/micropython/lib/libm/wf_lgamma.c",
                "external/micropython/lib/libm/wf_tgamma.c",
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
# Build+sign+embed stdlib.zip (mods/py/stdlib_src/) — not tracked in git,
# always freshly baked into the binary, see embed-stdlib.sh.
"${ROOT}/scripts/build.d/port/efi/embed-stdlib.sh"
# Dropbear static lib (PIC) for Metal.inf DLINK — X64 EFI.
PM_METAL_DROPBEAR_PIC=1 "${ROOT}/scripts/build.d/lib/dropbear.sh" x86_64
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
