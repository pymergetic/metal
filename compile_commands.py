#!/usr/bin/env python3
"""Emit clangd compile_commands.json for extmod/metal (µPy TUs vs host-test)."""
import json
import os
import pathlib
import sys

metal = pathlib.Path(sys.argv[1]).resolve()
host = pathlib.Path(sys.argv[2]).resolve()
wasm = pathlib.Path(sys.argv[3]).resolve()
ws = pathlib.Path(sys.argv[4]).resolve()
vscode_cdb = pathlib.Path(sys.argv[5])

upy = {
    "modmetal.c",
    "modmetal.h",
    "boot.c",
    "boot.h",
    "mpconfig_unix.h",
}
defs = (
    " -DMICROPY_PY_WASM=1 -DMICROPY_PY_METAL=1 -DMICROPY_SSL_MBEDTLS=1"
    ' -DMBEDTLS_CONFIG_FILE=\\"mbedtls/mbedtls_config_port.h\\"'
    " -DMICROPY_MODULE_BUILTIN_INIT=1 -DMICROPY_MODULE_BUILTIN_SUBPACKAGES=1"
    " -DPM_WASMMOD_GUEST=0 -DPM_MOD_TESTS=1 -D_POSIX_C_SOURCE=200809L"
)
card_incs = [
    metal / "src",
    wasm,
    wasm / "src",
    host,
    host / "ports/unix",
    host / "ports/unix/variants/standard",
    host / "ports/unix/build-wasm",
    host / "lib/mbedtls/include",
]
# net.zenoh card + its zenoh-pico platform shim. Same flags as the build's
# ZP_CPPFLAGS (tools/zenoh.mk / Makefile): the vendored include/src dirs, the
# card dir's GENERIC config, and -DZENOH_GENERIC so zenoh-pico/config.h picks
# the card dir's zenoh_generic_config.h instead of a board system layer.
zp_pico = host / "externals/zenoh-pico"
zenoh_incs = card_incs + [
    zp_pico / "include",
    zp_pico / "src",
    metal / "src/pymergetic/metal/net/zenoh",
]
zenoh_defs = defs + " -DZENOH_GENERIC"
upy_incs = [
    metal / "src",
    wasm,
    wasm / "src",
    host,
    host / "ports/unix",
    host / "ports/unix/variants/standard",
    host / "ports/unix/build-metal",
    host / "lib/mbedtls/include",
]
# Firmware µPy (port/upy, mpconfigport.h). unix build-wasm has no Q(metal).
fw_incs = [
    metal / "port",
    metal / "port/fwinc",
    metal / "port/boards/X86_64_BIOS",
    metal / "port/build/X86_64_BIOS-mp-repl",
    metal / "src",
    wasm,
    wasm / "src",
    host,
    host / "lib/mbedtls/include",
]
fw_defs = (
    " -DPM_METAL_FIRMWARE=1 -DPM_WASMMOD_GUEST=1 -DNDEBUG"
    " -DMICROPY_SSL_MBEDTLS=1"
    ' -DMBEDTLS_CONFIG_FILE=\\"mbedtls/mbedtls_config_port.h\\"'
)
wasm_h = wasm / "ports/micropython/mpconfig_wasm.h"
unix_h = metal / "mpconfig_unix.h"


def inc(paths):
    return " ".join("-I" + str(p) for p in paths)


def is_fw(f):
    try:
        return f.relative_to(metal).parts[0] == "port"
    except ValueError:
        return False


ents = []
for f in sorted(metal.rglob("*")):
    if f.suffix not in {".c", ".h", ".cpp"} or "build" in f.parts:
        continue
    if is_fw(f):
        pfx = (
            "clang -xc -std=gnu11 -ffreestanding -Wall -Wno-unknown-attributes "
            + inc(fw_incs)
            + fw_defs
        )
    elif "tools/mrustc_embed" in str(f.relative_to(metal)):
        # mrustc in-process embed shim (C++14, links mrustc.a). Must see
        # mrustc's own include dirs so clangd resolves <version.hpp>,
        # <span.hpp>, <ast/crate.hpp>, etc.
        mrustc_incs = [
            metal / "externals/mrustc/src/include",
            metal / "externals/mrustc/src",
            metal / "externals/mrustc/tools/common",
            metal / "tools/mrustc_embed",
        ]
        mrustc_defs = " -DPM_HAS_MRUSTC=1"
        pfx = (
            "clang -xc++ -std=c++14 -g -Wall -Wno-unused-parameter -Wno-sign-compare "
            + inc(mrustc_incs)
            + mrustc_defs
        )
    elif "src/pymergetic/metal/net/zenoh" in str(f.relative_to(metal)):
        # Both .c and .h under net/zenoh: the platform card headers reference the
        # vendored tree (zenoh_generic_platform.h -> "zenoh-pico/config.h"), so a
        # .h-only clangd TU must get the same ZENOH_GENERIC + pico include dirs as
        # the .c faces or clangd reports 'zenoh-pico/config.h' file not found.
        pfx = "clang -xc -std=gnu11 -Wall -Wno-unknown-attributes " + inc(zenoh_incs) + zenoh_defs
    elif "tools/zp_pico_prove" in str(f.relative_to(metal)):
        # Standalone Stage-1 proofs compile against the vendored lib's own unix
        # layer (-DZENOH_LINUX, mirroring tools/zp_pico_prove/Makefile ZPFLAGS),
        # not the Metal card's ZENOH_GENERIC platform shim. Falls to the generic
        # µPy flags otherwise, which hides <zenoh-pico.h> and z_loaned_sample_t.
        prove_incs = [zp_pico / "include"]
        pfx = (
            "clang -xc -std=gnu11 -Wall -Wno-unknown-attributes "
            + inc(prove_incs)
            + " -DZENOH_LINUX"
        )
    elif f.name in upy:
        pfx = (
            "clang -xc -std=gnu11 -Wall -Wno-unknown-attributes "
            + inc(upy_incs)
            + defs
            + " -include "
            + str(wasm_h)
            + " -include "
            + str(unix_h)
        )
    else:
        pfx = "clang -xc -std=gnu11 -Wall -Wno-unknown-attributes " + inc(card_incs) + defs
    ents.append({"directory": str(ws), "file": str(f), "command": pfx + " -c " + str(f)})

(metal / "compile_commands.json").write_text(json.dumps(ents, indent=2) + "\n")
data = json.loads(vscode_cdb.read_text()) if vscode_cdb.exists() else []
pref = str(metal) + os.sep
kept = [
    e
    for e in data
    if not str(e.get("file", "")).startswith(pref)
    and "packages/micropython-wasmmod/extmod/metal/" not in str(e.get("file", ""))
]
vscode_cdb.write_text(json.dumps(ents + kept, indent=4) + "\n")
print("metal clangd TUs", len(ents))
