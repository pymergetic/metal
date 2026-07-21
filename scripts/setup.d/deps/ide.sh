#!/usr/bin/env bash
# Generate .clangd (and a tiny compile_commands for metal.efi) for this checkout.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
EDK2_INC="${ROOT}/external/edk2/MdePkg/Include"
TLSF_INC="${ROOT}/external/tlsf"
METAL_SRC="${ROOT}/src/pymergetic/metal"
METAL_EFI_MEM="${ROOT}/src/efi/pymergetic/metal/mem"
METAL_EFI_WAMR="${ROOT}/src/efi/pymergetic/metal/wamr"
WAMR_PLAT_INC="${ROOT}/src/efi/wamr/core/shared/platform/include"
HOST_STUBS="${METAL_EFI_MEM}/host_stubs"
METAL_PKG="${ROOT}/src/efi/MetalPkg"
MAIN="${METAL_PKG}/main.c"
TLSF_C="${METAL_EFI_MEM}/tlsf_edk2.c"
MERGED="${ROOT}/build/compile_commands.json"

mkdir -p "${ROOT}/build"

# Tiny CDB so editors that ignore .clangd still find Uefi.h / WAMR platform headers.
python3 - <<PY
import json
from pathlib import Path

root = Path(${ROOT@Q})
edk2 = root / "external/edk2/MdePkg/Include"
edk2_x64 = edk2 / "X64"
tlsf = root / "external/tlsf"
metal_src = root / "src/pymergetic/metal"
efi_mem = root / "src/efi/pymergetic/metal/mem"
efi_wamr = root / "src/efi/pymergetic/metal/wamr"
wamr_plat = root / "src/efi/wamr/core/shared/platform/include"
wamr_utils = root / "src/efi/wamr/core/shared/utils"
wamr_iwasm = root / "src/efi/wamr/core/iwasm/include"
wamr_wasi = root / "src/efi/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include"
stubs = efi_mem / "host_stubs"
pkg = root / "src/efi/MetalPkg"
inc_root = root / "include"
out = root / "build/compile_commands.json"
efi_net = root / "src/efi/pymergetic/metal/net"
lwip_inc = root / "src/efi/lwip/src/include"
inc = (
    f"-I{stubs} -I{edk2} -I{edk2_x64} -I{tlsf} -I{inc_root} "
    f"-I{metal_src} -I{pkg} -I{efi_mem} -I{efi_net} -I{lwip_inc} "
    f"-I{efi_wamr} -I{wamr_plat} "
    f"-I{wamr_utils} -I{wamr_iwasm} -I{wamr_wasi} "
    f"-DBH_PLATFORM_METAL_EFI -DBH_PLATFORM_ZEPHYR "
    f"-DBUILD_TARGET_X86_64 -DWASM_ENABLE_LIBC_WASI=1 "
)
base = (
    "/usr/bin/clang -std=c11 -ffreestanding -fno-stack-protector "
    "-target x86_64-unknown-windows "
    "'-DEFIAPI=__attribute__((ms_abi))' "
    f"{inc} "
)
entries = []
for rel in (
    "src/efi/MetalPkg/main.c",
    "src/efi/pymergetic/metal/mem/tlsf_edk2.c",
    "src/efi/pymergetic/metal/net/lwip_sys.c",
    "src/efi/pymergetic/metal/net/net_lwip.c",
    "src/efi/pymergetic/metal/wamr/efi_platform.c",
    "src/efi/pymergetic/metal/wamr/efi_thread.c",
    "src/efi/pymergetic/metal/wamr/efi_socket.c",
    "src/efi/pymergetic/metal/wamr/efi_wasi_fs.c",
):
    fpath = (root / rel).resolve()
    if fpath.is_file() and edk2.is_dir():
        entries.append({
            "directory": str(root),
            "command": f"{base}-c -o /dev/null {fpath}",
            "file": str(fpath),
        })

# doomgeneric Metal guests (wasm) — so editors resolve doomgeneric.h.
wasi_sys = root / ".tools/wasi-sdk/share/wasi-sysroot"
dg_inc = root / "external/doomgeneric/doomgeneric"
doom_stubs = root / "mods/apps/doom/ide_stubs"
doom_base = (
    f"/usr/bin/clang -std=c11 --target=wasm32-wasip1 --sysroot={wasi_sys} "
    f"-I{inc_root} -I{dg_inc} -I{doom_stubs} "
    f"-DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE "
    f"-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 "
)
if wasi_sys.is_dir() and dg_inc.is_dir():
    for rel in (
        "mods/apps/doom/doomgeneric_metal.c",
        "mods/apps/doom/metal_main.c",
        "mods/apps/doom/w_file_metal.c",
        "mods/apps/doom/m_fileexists_metal.c",
    ):
        fpath = (root / rel).resolve()
        if fpath.is_file():
            entries.append({
                "directory": str(root),
                "command": f"{doom_base}-c -o /dev/null {fpath}",
                "file": str(fpath),
            })

out.write_text(json.dumps(entries, indent=2) + "\n")
print(f"compile_commands.json: {len(entries)} entr(y/ies) -> {out}")
if not (root / "external/edk2/MdePkg/Include/Uefi.h").is_file():
    print("note: external/edk2 missing — run ./scripts/setup edk2 for Uefi.h")
if not (tlsf / "tlsf.h").is_file():
    print("note: external/tlsf missing — tlsf_edk2.c needs tlsf.h")
PY

ln -sfn "${MERGED}" "${ROOT}/compile_commands.json"
echo "compile_commands.json -> ${MERGED}"

sed "s|@@ROOT@@|${ROOT}|g" "${ROOT}/.clangd.template" > "${ROOT}/.clangd"
echo ".clangd -> generated from .clangd.template (ROOT=${ROOT})"

CLANG_EFI=(
	clang -fsyntax-only -std=c11 -ffreestanding -fno-stack-protector
	-target x86_64-unknown-windows
	'-DEFIAPI=__attribute__((ms_abi))'
	-I"${HOST_STUBS}"
	-I"${EDK2_INC}" -I"${EDK2_INC}/X64"
	-I"${TLSF_INC}"
	-I"${ROOT}/include"
	-I"${METAL_SRC}"
	-I"${METAL_PKG}"
	-I"${METAL_EFI_MEM}"
	-I"${ROOT}/src/efi/pymergetic/metal/net"
	-I"${ROOT}/src/efi/lwip/src/include"
	-I"${METAL_EFI_WAMR}"
	-I"${WAMR_PLAT_INC}"
	-I"${ROOT}/src/efi/wamr/core/shared/utils"
	-I"${ROOT}/src/efi/wamr/core/iwasm/include"
	-I"${ROOT}/src/efi/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include"
	-DBH_PLATFORM_METAL_EFI
	-DBH_PLATFORM_ZEPHYR
	-DBUILD_TARGET_X86_64
	-DWASM_ENABLE_LIBC_WASI=1
)

if [[ -f "${EDK2_INC}/Uefi.h" && -f "${MAIN}" ]]; then
	"${CLANG_EFI[@]}" "${MAIN}"
	echo "ide: clang -fsyntax-only ok (${MAIN})"
	if [[ -f "${TLSF_C}" && -f "${TLSF_INC}/tlsf.h" ]]; then
		"${CLANG_EFI[@]}" "${TLSF_C}"
		echo "ide: clang -fsyntax-only ok (${TLSF_C})"
	fi
	LWIP_SYS="${ROOT}/src/efi/pymergetic/metal/net/lwip_sys.c"
	if [[ -f "${LWIP_SYS}" && -f "${ROOT}/src/efi/lwip/src/include/lwip/sys.h" ]]; then
		"${CLANG_EFI[@]}" "${LWIP_SYS}"
		echo "ide: clang -fsyntax-only ok (${LWIP_SYS})"
	fi
	if [[ -f "${WAMR_PLAT_INC}/platform_api_vmcore.h" ]]; then
		for f in \
			"${METAL_EFI_WAMR}/efi_platform.c" \
			"${METAL_EFI_WAMR}/efi_thread.c" \
			"${METAL_EFI_WAMR}/efi_socket.c" \
			"${METAL_EFI_WAMR}/efi_wasi_fs.c"
		do
			if [[ -f "${f}" ]]; then
				"${CLANG_EFI[@]}" "${f}"
				echo "ide: clang -fsyntax-only ok (${f})"
			fi
		done
	fi
else
	echo "ide: skip syntax check (edk2 or main.c missing)"
fi
