#!/usr/bin/env bash
# Generate .clangd (and a tiny compile_commands for metal.efi) for this checkout.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
EDK2_INC="${ROOT}/external/edk2/MdePkg/Include"
TLSF_INC="${ROOT}/external/tlsf"
METAL_SRC="${ROOT}/src/pymergetic/metal"
METAL_EFI_MEM="${ROOT}/src/pymergetic/metal/runtime/mem"
METAL_EFI_WAMR="${ROOT}/src/pymergetic/metal/guest/wamr"
WAMR_PLAT_INC="${ROOT}/external/wamr/core/shared/platform/include"
HOST_STUBS="${METAL_EFI_MEM}/host_stubs"
METAL_PKG="${ROOT}/src/efi/MetalPkg"
MAIN="${METAL_PKG}/main.c"
TLSF_C="${METAL_EFI_MEM}/tlsf_edk2.c"
MERGED="${ROOT}/build/compile_commands.json"

mkdir -p "${ROOT}/build"

# Ensure build/trust/metal_trust_bake.inc.c exists for trust.c / clangd.
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/pki.sh"
pm_metal_pki_bake || true

# Tiny CDB so editors that ignore .clangd still find Uefi.h / WAMR platform headers.
python3 - <<PY
import json
from pathlib import Path

root = Path(${ROOT@Q})
edk2 = root / "external/edk2/MdePkg/Include"
edk2_x64 = edk2 / "X64"
tlsf = root / "external/tlsf"
metal_src = root / "src/pymergetic/metal"
efi_mem = root / "src/pymergetic/metal/runtime/mem"
efi_wamr = root / "src/pymergetic/metal/guest/wamr"
wamr_plat = root / "external/wamr/core/shared/platform/include"
wamr_utils = root / "external/wamr/core/shared/utils"
wamr_iwasm = root / "external/wamr/core/iwasm/include"
wamr_wasi = root / "external/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include"
stubs = efi_mem / "host_stubs"
pkg = root / "src/efi/MetalPkg"
inc_root = root / "include"
out = root / "build/compile_commands.json"
efi_net = root / "src/pymergetic/metal/dev/net"
lwip_inc = root / "external/lwip/src/include"
mbedtls_inc = root / "external/mbedtls/include"
mbedtls_cfg = (
    "-DMBEDTLS_CONFIG_FILE=<pymergetic/metal/dev/net/mbedtls_metal_config.h>"
)
inc = (
    f"-I{stubs} -I{edk2} -I{edk2_x64} -I{tlsf} -I{inc_root} "
    f"-I{metal_src} -I{pkg} -I{efi_mem} -I{efi_net} -I{lwip_inc} "
    f"-I{mbedtls_inc} {mbedtls_cfg} "
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
# Exact per-file CDB for all Metal EFI sources — otherwise clangd infers from
# a BIOS sibling (PmBiosUefi.h) or skips EDK2 -I and reports missing Uefi.h.
if edk2.is_dir():
    efi_files = []
    for base_dir in (
        root / "src/efi/MetalPkg",
        root / "src/efi/pymergetic/metal",
        efi_net,
    ):
        if base_dir.is_dir():
            efi_files.extend(sorted(base_dir.rglob("*.c")))
    for fpath in efi_files:
        if fpath.is_file():
            rp = fpath.resolve()
            entries.append({
                "directory": str(root),
                "command": f"{base}-c -o /dev/null {rp}",
                "file": str(rp),
            })

# doomgeneric Metal guests (wasm) — so editors resolve doomgeneric.h / wasi.
wasi_sys = root / ".tools/wasi-sdk/share/wasi-sysroot"
dg_inc = root / "external/doomgeneric/doomgeneric"
doom_dir = root / "mods/apps/doom"
doom_stubs = doom_dir / "ide_stubs"
doom_base = (
    f"/usr/bin/clang -std=c11 --target=wasm32-wasip1 --sysroot={wasi_sys} "
    f"-I{inc_root} -I{dg_inc} -I{doom_dir} -I{doom_stubs} "
    f"-DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L "
    f"-DFEATURE_SOUND "
    f"-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 "
)
if wasi_sys.is_dir() and dg_inc.is_dir() and doom_dir.is_dir():
    for fpath in sorted(doom_dir.glob("*.c")):
        rp = fpath.resolve()
        entries.append({
            "directory": str(root),
            "command": f"{doom_base}-c -o /dev/null {rp}",
            "file": str(rp),
        })

# Metal BIOS shim — PmBiosUefi.h under src/bios/shim (not EDK2 Uefi.h).
# Exact per-file CDB entries are required: clangd otherwise reuses the EFI
# sibling (e.g. efi/.../net/net_lwip.c) and misses PmBiosUefi.h.
bios_shim = root / "src/bios/shim"
bios_metal = root / "src/bios/pymergetic/metal"
bios_net = root / "src/pymergetic/metal/dev/net"
bios_inc = (
    f"-I{bios_shim} -I{inc_root} -I{metal_src} "
    f"-I{bios_net} -I{lwip_inc} -I{mbedtls_inc} {mbedtls_cfg} "
    f"-I{bios_metal / 'guest/wamr'} "
    f"-I{bios_metal / 'runtime/mem/host_stubs'} "
    f"-I{wamr_iwasm} -I{wamr_plat} -I{wamr_utils} "
    f"-DBH_PLATFORM_METAL_BIOS -DBH_PLATFORM_METAL_EFI "
    f"-DBH_PLATFORM_ZEPHYR -DBUILD_TARGET_X86_64 "
    f"-DWASM_ENABLE_LIBC_WASI=1 "
)
bios_base = (
    "/usr/bin/clang -std=c11 -ffreestanding -fno-stack-protector -m64 "
    f"{bios_inc}"
)
bios_files = []
for base_dir in (
    root / "src/bios/shim",
    root / "src/bios/BiosPkg",
    bios_metal,
):
    if base_dir.is_dir():
        bios_files.extend(sorted(base_dir.rglob("*.c")))
for fpath in bios_files:
    if fpath.is_file():
        entries.append({
            "directory": str(root),
            "command": f"{bios_base}-c -o /dev/null {fpath.resolve()}",
            "file": str(fpath.resolve()),
        })

# Host-side regressions (Linux glibc — not freestanding / WASI).
host_dir = root / "tests/host"
host_base = (
    f"/usr/bin/clang -std=c11 --target=x86_64-linux-gnu -D_GNU_SOURCE "
    f"-I{tlsf} -I{inc_root} "
)
if host_dir.is_dir():
    for fpath in sorted(host_dir.glob("*.c")):
        rp = fpath.resolve()
        cmd = f"{host_base}-c -o /dev/null {rp}"
        # metal001 also compiles vendored tlsf.c at verify time; for IDE
        # the header -I is enough to resolve includes.
        entries.append({
            "directory": str(root),
            "command": cmd,
            "file": str(rp),
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

if [[ -f "${ROOT}/tests/host/.clangd.template" ]]; then
	sed "s|@@ROOT@@|${ROOT}|g" "${ROOT}/tests/host/.clangd.template" \
		> "${ROOT}/tests/host/.clangd"
	echo "tests/host/.clangd -> generated from tests/host/.clangd.template"
fi

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
	-I"${ROOT}/src/pymergetic/metal/dev/net"
	-I"${ROOT}/external/lwip/src/include"
	-I"${ROOT}/external/mbedtls/include"
	'-DMBEDTLS_CONFIG_FILE=<pymergetic/metal/dev/net/mbedtls_metal_config.h>'
	-I"${METAL_EFI_WAMR}"
	-I"${WAMR_PLAT_INC}"
	-I"${ROOT}/external/wamr/core/shared/utils"
	-I"${ROOT}/external/wamr/core/iwasm/include"
	-I"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include"
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
	LWIP_SYS="${ROOT}/src/pymergetic/metal/dev/net/lwip_sys.c"
	if [[ -f "${LWIP_SYS}" && -f "${ROOT}/external/lwip/src/include/lwip/sys.h" ]]; then
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
