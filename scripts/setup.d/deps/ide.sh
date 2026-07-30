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

# Kconfig → build/autoconf.h (limits / iface toggles) for clangd -include.
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/kconfig.sh"
pm_metal_kconfig_ensure || true

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
ssh_dir = metal_src / "net/ssh"
db_stubs = ssh_dir / "dropbear_stubs"
db_metal = ssh_dir / "dropbear_metal"
db_src = root / "external/dropbear/src"
lwip_inc = root / "external/lwip/src/include"
net_ip = metal_src / "net/ip"
mbedtls_inc = root / "external/mbedtls/include"
mbedtls_cfg = (
    "-DMBEDTLS_CONFIG_FILE=<pymergetic/metal/net/tls/mbedtls_metal_config.h>"
)
inc_common = (
    f"-I{edk2} -I{edk2_x64} -I{tlsf} -I{inc_root} "
    f"-I{metal_src} -I{pkg} -I{efi_mem} -I{net_ip} -I{lwip_inc} "
    f"-I{mbedtls_inc} {mbedtls_cfg} "
    f"-I{efi_wamr} -I{wamr_plat} "
    f"-I{wamr_utils} -I{wamr_iwasm} -I{wamr_wasi} "
    f"-DBH_PLATFORM_METAL_EFI "
    f"-DAPP_THREAD_STACK_SIZE_DEFAULT=6144 -DAPP_THREAD_STACK_SIZE_MIN=4096 "
    f"-DBUILD_TARGET_X86_64 -DWASM_ENABLE_LIBC_WASI=1 "
)
# host_stubs first is wrong for Dropbear glue — freestanding POSIX types
# live under dropbear_stubs (uid_t, struct timeval, …).
inc = f"-I{stubs} {inc_common}"
db_ltc = root / "external/dropbear/libtomcrypt/src/headers"
db_ltm = root / "external/dropbear/libtommath"
inc_dropbear = (
    f"-DDROPBEAR_METAL=1 -DDROPBEAR_SERVER=1 -DLOCALOPTIONS_H_EXISTS=1 "
    f"-DBUNDLED_LIBTOM=1 -DUSE_LTM -DLTM_DESC "
    f"-I{db_metal} -I{db_stubs} -I{stubs} {inc_common} "
    f"-I{db_src} -I{db_ltc} -I{db_ltm} "
)
clang_efi = (
    "/usr/bin/clang -std=c11 -ffreestanding -fno-stack-protector "
    "-target x86_64-unknown-windows "
    "'-DEFIAPI=__attribute__((ms_abi))' "
)
base = f"{clang_efi}{inc} "
base_dropbear = f"{clang_efi}{inc_dropbear} "

def is_dropbear_glue(fpath: Path) -> bool:
    parts = fpath.parts
    if "dropbear_stubs" in parts or "dropbear_metal" in parts:
        return True
    name = fpath.name
    return name.startswith("dropbear_") or name.startswith("ssh_dropbear")

entries = []
# Exact per-file CDB for all Metal EFI sources — otherwise clangd infers from
# a BIOS sibling (PmBiosUefi.h) or skips EDK2 -I and reports missing Uefi.h.
if edk2.is_dir():
    efi_files = []
    for base_dir in (
        root / "src/efi/MetalPkg",
        root / "src/efi/pymergetic/metal",
        metal_src,
    ):
        if base_dir.is_dir():
            efi_files.extend(sorted(base_dir.rglob("*.c")))
    for fpath in efi_files:
        if fpath.is_file():
            rp = fpath.resolve()
            cmd_base = base_dropbear if is_dropbear_glue(fpath) else base
            entries.append({
                "directory": str(root),
                "command": f"{cmd_base}-c -o /dev/null {rp}",
                "file": str(rp),
            })

# Vendored Dropbear sources — Metal config.h/stubs, not host autoconf.
if db_src.is_dir():
    for fpath in sorted(db_src.glob("*.c")):
        rp = fpath.resolve()
        entries.append({
            "directory": str(root),
            "command": f"{base_dropbear}-c -o /dev/null {rp}",
            "file": str(rp),
        })

# External apps (e.g. ../metal-doom) live in a sibling repo now — no
# in-tree source for clangd to index here.
wasi_sys = root / ".tools/wasi-sdk/share/wasi-sysroot"

# mods/tests/*/main.c (wasm32-wasip1-threads) — same target as
# scripts/build.d/guest/mod.sh, otherwise clangd (no --target) treats these
# as host C and picks the wrong (host-pointer) side of every wasm/host
# dual-ABI header split (e.g. fs.h's pm_metal_fs_read_async: uint32_t on
# the wasm side, void* on the host side).
tests_dir = root / "mods/tests"
wamr_socket_inc = root / "external/wamr/core/iwasm/libraries/lib-socket/inc"
tests_base = (
    f"/usr/bin/clang -std=c11 --target=wasm32-wasip1-threads --sysroot={wasi_sys} "
    f"-pthread -I{inc_root} "
)
if wasi_sys.is_dir() and tests_dir.is_dir():
    for mod_dir in sorted(p for p in tests_dir.iterdir() if p.is_dir()):
        if (mod_dir / "ARCHIVED").is_file():
            continue
        fpath = mod_dir / "main.c"
        if not fpath.is_file():
            continue
        extra = f"-I{wamr_socket_inc} " if (mod_dir / "SOCKET").is_file() else ""
        rp = fpath.resolve()
        entries.append({
            "directory": str(root),
            "command": f"{tests_base}{extra}-c -o /dev/null {rp}",
            "file": str(rp),
        })

# Metal BIOS shim — PmBiosUefi.h under src/bios/shim (not EDK2 Uefi.h).
# Exact per-file CDB entries are required: clangd otherwise reuses the EFI
# sibling (e.g. efi/.../net/net_lwip.c) and misses PmBiosUefi.h.
bios_shim = root / "src/bios/shim"
bios_metal = root / "src/bios/pymergetic/metal"
bios_inc = (
    f"-I{bios_shim} -I{inc_root} -I{metal_src} "
    f"-I{net_ip} -I{lwip_inc} -I{mbedtls_inc} {mbedtls_cfg} "
    f"-I{bios_metal / 'guest/wamr'} "
    f"-I{bios_metal / 'runtime/mem/host_stubs'} "
    f"-I{wamr_iwasm} -I{wamr_plat} -I{wamr_utils} "
    f"-DBH_PLATFORM_METAL_BIOS -DBH_PLATFORM_METAL_EFI "
    f"-DAPP_THREAD_STACK_SIZE_DEFAULT=6144 -DAPP_THREAD_STACK_SIZE_MIN=4096 "
    f"-DBUILD_TARGET_X86_64 "
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

# exp2 freestanding tree — exact CDB entries so clangd does not fuzzy-match
# product EFI/BIOS siblings (wrong mem prototypes / missing metal/libc).
exp2_src = root / "exp2/src"
exp2_libc = root / "exp2/src/pymergetic/metal/libc"
exp2_net_ip = root / "exp2/src/pymergetic/metal/net/ip"
exp2_efi = root / "exp2/src/pymergetic/metal/boot/platform/efi"
exp2_bios = root / "exp2/src/pymergetic/metal/boot/platform/bios"
exp2_priv = root / "exp2/src/pymergetic/metal/boot/platform/private"
exp2_common = (
    f"/usr/bin/clang -std=c11 -ffreestanding -nostdinc -fno-stack-protector "
    f"-m64 -mno-red-zone "
    f"-I{exp2_libc} -I{exp2_src} -I{exp2_net_ip} -I{exp2_net_ip / 'cfg'} "
    f"-I{lwip_inc} "
)
exp2_efi_flags = (
    f"{exp2_common}--target=x86_64-unknown-windows -fshort-wchar "
    f"-Wno-macro-redefined -DPM_METAL_BOOT_TARGET_EFI=1 "
    f"-I{exp2_efi} -I{edk2} -I{edk2_x64} "
    f"-I{edk2 / 'Protocol'} -I{edk2 / 'Guid'} "
)
exp2_bios_flags = (
    f"{exp2_common}-DPM_METAL_BOOT_TARGET_BIOS=1 -I{exp2_bios} "
)
if exp2_src.is_dir():
    for fpath in sorted(exp2_src.rglob("*.c")):
        if not fpath.is_file():
            continue
        parts = fpath.parts
        if ".pm" in parts or ".target" in parts:
            continue
        rp = fpath.resolve()
        rel = str(fpath.relative_to(root))
        if "/boot/platform/efi/" in rel:
            cmd_base = exp2_efi_flags
        elif "/boot/platform/bios/" in rel or "/boot/platform/private/" in rel:
            cmd_base = exp2_bios_flags
        else:
            cmd_base = exp2_common
        entries.append({
            "directory": str(root),
            "command": f"{cmd_base}-c -o /dev/null {rp}",
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
# Zephyr-IDE / metal .vscode settings force:
#   clangd --compile-commands-dir=${workspaceFolder}/.vscode
# Without this symlink clangd finds no CDB there and drops -Ilwip / metal/libc.
mkdir -p "${ROOT}/.vscode"
ln -sfn ../build/compile_commands.json "${ROOT}/.vscode/compile_commands.json"
echo ".vscode/compile_commands.json -> ../build/compile_commands.json"

# Repo-relative -I only (resolved against each .clangd file's directory).
# Never expand checkout-absolute paths into .clangd*.
cp "${ROOT}/.clangd.template" "${ROOT}/.clangd"
echo ".clangd -> copied from .clangd.template (repo-relative -I)"

# Closest-wins fragment for exp2 net (lwIP). Paths relative to this file.
EXP2_NET_CLANGD="${ROOT}/exp2/src/pymergetic/metal/net/.clangd"
if [[ -d "${ROOT}/exp2/src/pymergetic/metal/net" ]]; then
	cat > "${EXP2_NET_CLANGD}" <<'EOF'
# Generated by scripts/setup ide — do not hand-edit.
# -I paths are relative to this file (no checkout-absolute paths).
CompileFlags:
  Add:
    - -xc
    - -std=c11
    - -ffreestanding
    - -nostdinc
    - -m64
    - -mno-red-zone
    - -I../libc
    - -I../../..
    - -Iip
    - -Iip/cfg
    - -I../../../../../external/lwip/src/include
  Remove:
    - -Isrc/pymergetic/metal
Diagnostics:
  UnusedIncludes: None
  MissingIncludes: None
EOF
	echo "exp2 net .clangd -> ${EXP2_NET_CLANGD}"
fi

# Closest-wins fragment for exp2 littlefs (LFS_CONFIG + vendor).
EXP2_LFS_CLANGD="${ROOT}/exp2/src/pymergetic/metal/fs/littlefs/.clangd"
if [[ -d "${ROOT}/exp2/src/pymergetic/metal/fs/littlefs" ]]; then
	cat > "${EXP2_LFS_CLANGD}" <<'EOF'
# Generated by scripts/setup ide — do not hand-edit.
# -I paths are relative to this file (no checkout-absolute paths).
CompileFlags:
  Add:
    - -xc
    - -std=c11
    - -ffreestanding
    - -nostdinc
    - -m64
    - -mno-red-zone
    - -I../../libc
    - -I.
    - -Ivendor
    - -DLFS_CONFIG=lfs_config.h
    - -DPM_METAL_LFS_FREESTANDING
  Remove:
    - -Isrc/pymergetic/metal
Diagnostics:
  UnusedIncludes: None
  MissingIncludes: None
EOF
	echo "exp2 littlefs .clangd -> ${EXP2_LFS_CLANGD}"
fi

if [[ -f "${ROOT}/tests/host/.clangd.template" ]]; then
	cp "${ROOT}/tests/host/.clangd.template" "${ROOT}/tests/host/.clangd"
	echo "tests/host/.clangd -> copied from tests/host/.clangd.template"
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
	-I"${ROOT}/src/pymergetic/metal/net/ip"
	-I"${ROOT}/external/lwip/src/include"
	-I"${ROOT}/external/mbedtls/include"
	'-DMBEDTLS_CONFIG_FILE=<pymergetic/metal/net/tls/mbedtls_metal_config.h>'
	-I"${METAL_EFI_WAMR}"
	-I"${WAMR_PLAT_INC}"
	-I"${ROOT}/external/wamr/core/shared/utils"
	-I"${ROOT}/external/wamr/core/iwasm/include"
	-I"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include"
	-DBH_PLATFORM_METAL_EFI
	-DAPP_THREAD_STACK_SIZE_DEFAULT=6144
	-DAPP_THREAD_STACK_SIZE_MIN=4096
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
	LWIP_SYS="${ROOT}/src/pymergetic/metal/net/ip/lwip_sys.c"
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

echo "ide: done — restart clangd (or reload the window) so the IDE picks up .clangd"

