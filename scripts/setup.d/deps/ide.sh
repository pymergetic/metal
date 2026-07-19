#!/usr/bin/env bash
# Generate .clangd (and a tiny compile_commands for metal.efi) for this checkout.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
EDK2_INC="${ROOT}/external/edk2/MdePkg/Include"
TLSF_INC="${ROOT}/external/tlsf"
METAL_PKG="${ROOT}/src/efi/MetalPkg"
MAIN="${METAL_PKG}/main.c"
MEM_C="${METAL_PKG}/mem/metal_mem.c"
MERGED="${ROOT}/build/compile_commands.json"

mkdir -p "${ROOT}/build"

# Tiny CDB so editors that ignore .clangd still find Uefi.h / tlsf.h.
python3 - <<PY
import json
from pathlib import Path

root = Path(${ROOT@Q})
edk2 = root / "external/edk2/MdePkg/Include"
edk2_x64 = edk2 / "X64"
tlsf = root / "external/tlsf"
pkg = root / "src/efi/MetalPkg"
mem = pkg / "mem"
out = root / "build/compile_commands.json"
inc = f"-I{edk2} -I{edk2_x64} -I{tlsf} -I{pkg} -I{mem}"
base = (
    "/usr/bin/clang -std=c11 -ffreestanding -fno-stack-protector "
    "-target x86_64-unknown-windows "
    "'-DEFIAPI=__attribute__((ms_abi))' "
    f"{inc} "
)
entries = []
for rel in ("src/efi/MetalPkg/main.c", "src/efi/MetalPkg/mem/metal_mem.c"):
    fpath = (root / rel).resolve()
    if fpath.is_file() and edk2.is_dir():
        entries.append({
            "directory": str(root),
            "command": f"{base}-c -o /dev/null {fpath}",
            "file": str(fpath),
        })
out.write_text(json.dumps(entries, indent=2) + "\n")
print(f"compile_commands.json: {len(entries)} entr(y/ies) -> {out}")
if not (root / "external/edk2/MdePkg/Include/Uefi.h").is_file():
    print("note: external/edk2 missing — run ./scripts/setup edk2 for Uefi.h")
if not (tlsf / "tlsf.h").is_file():
    print("note: external/tlsf missing — metal_mem.c needs tlsf.h")
PY

ln -sfn "${MERGED}" "${ROOT}/compile_commands.json"
echo "compile_commands.json -> ${MERGED}"

sed "s|@@ROOT@@|${ROOT}|g" "${ROOT}/.clangd.template" > "${ROOT}/.clangd"
echo ".clangd -> generated from .clangd.template (ROOT=${ROOT})"

CLANG_EFI=(
	clang -fsyntax-only -std=c11 -ffreestanding -fno-stack-protector
	-target x86_64-unknown-windows
	'-DEFIAPI=__attribute__((ms_abi))'
	-I"${EDK2_INC}" -I"${EDK2_INC}/X64"
	-I"${TLSF_INC}" -I"${METAL_PKG}" -I"${METAL_PKG}/mem"
)

if [[ -f "${EDK2_INC}/Uefi.h" && -f "${MAIN}" ]]; then
	"${CLANG_EFI[@]}" "${MAIN}"
	echo "ide: clang -fsyntax-only ok (${MAIN})"
	if [[ -f "${MEM_C}" && -f "${TLSF_INC}/tlsf.h" ]]; then
		"${CLANG_EFI[@]}" "${MEM_C}"
		echo "ide: clang -fsyntax-only ok (${MEM_C})"
	fi
else
	echo "ide: skip syntax check (edk2 or main.c missing)"
fi
