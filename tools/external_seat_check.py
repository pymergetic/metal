#!/usr/bin/env python3
"""Prove declared external source sets produced objects on every built seat."""
from __future__ import annotations
from pathlib import Path
import re
import sys

METAL = Path(__file__).resolve().parent.parent
TOP = METAL.parent.parent
WASMMOD = TOP / "extmod" / "wasmmod"


def fail(message: str) -> None:
    print(f"external seat check: {message}", file=sys.stderr)
    raise SystemExit(1)


def make_words(path: Path, name: str, prefix: str = "", occurrence: int = 1) -> list[str]:
    lines = path.read_text().splitlines()
    for index, line in enumerate(lines):
        match = re.match(rf"^{re.escape(name)}\s*[:+?]?=\s*(.*)$", line)
        if not match:
            continue
        occurrence -= 1
        if occurrence != 0:
            continue
        parts = [match.group(1)]
        while parts[-1].rstrip().endswith("\\"):
            index += 1
            if index >= len(lines):
                fail(f"{path}: unterminated {name}")
            parts.append(lines[index])
        value = " ".join(part.rstrip().removesuffix("\\") for part in parts)
        words = []
        for word in value.split():
            word = word.rstrip(",)")
            if re.search(r"\.(?:c|cc|cpp|s|S|o)$", word):
                words.append(prefix + word)
        return words
    fail(f"{path}: no {name}")


def object_rel(root: Path) -> set[str]:
    if not root.is_dir():
        fail(f"missing built seat {root}")
    return {str(path.relative_to(root)) for path in root.rglob("*.o")}


def require_objects(seat: str, actual: set[str], expected: list[str]) -> None:
    missing = sorted(item for item in expected if item not in actual)
    if missing:
        fail(f"{seat}: {len(missing)} declared objects absent: {', '.join(missing[:8])}")
    print(f"external seat check: {seat}: {len(expected)} declared objects present")


def source_objects(sources: list[str]) -> list[str]:
    return [re.sub(r"\.(?:c|cc|cpp|s|S)$", ".o", source) for source in sources]


def rust_dependencies(depfile: Path, roots: tuple[str, ...], seat: str) -> None:
    if not depfile.is_file():
        fail(f"{seat}: missing Rust dep-info {depfile}")
    data = depfile.read_text().replace("\\\n", " ")
    hits = {token for token in data.split() if token.endswith(".rs") and any(root in token for root in roots)}
    if not hits:
        fail(f"{seat}: Rust archive names no wasmmod source")
    print(f"external seat check: {seat}: Rust archive tracks {len(hits)} source files")


host = METAL / "build"
unix = TOP / "ports" / "unix" / "build-metal"
browser = TOP / "ports" / "webassembly" / "build-metal"
bios = METAL / "port" / "build" / "X86_64_BIOS-mp-repl"
uefi = METAL / "port" / "build" / "X86_64_UEFI-mp-repl"
actual = {name: object_rel(root) for name, root in (("host", host), ("unix", unix),
          ("browser", browser), ("bios", bios), ("uefi", uefi))}

# mbedTLS: hosted and browser use the full supported library set; firmware has
# an explicit freestanding subset. Compare the owning Make variables to objects.
mbed_host = make_words(METAL / "Makefile", "MBEDTLS_LIB_SRCS")
require_objects("host mbedtls", actual["host"], [f"mbedtls/{x[:-2]}.o" for x in mbed_host])
mbed_upy = make_words(METAL / "metal.mk", "SRC_METAL_MBEDTLS")
if set(mbed_host) != set(mbed_upy):
    fail("host and MicroPython hosted mbedTLS source profiles differ")
for seat in ("unix", "browser"):
    require_objects(f"{seat} mbedtls", actual[seat],
                    [f"lib/mbedtls/library/{x[:-2]}.o" for x in mbed_upy])
mbed_fw = make_words(METAL / "port" / "fw_mbedtls.mk", "FW_MBEDTLS_SRCS")
if not set(mbed_fw) < set(mbed_host):
    fail("freestanding mbedTLS profile is not a strict hosted subset")
for seat in ("bios", "uefi"):
    require_objects(f"{seat} mbedtls", actual[seat], [f"mbedtls/{x[:-2]}.o" for x in mbed_fw])

# TLSF is the one C muscle source for util.mem on every seat.
require_objects("browser tlsf", actual["browser"], ["extmod/wasmmod/third_party/tlsf/tlsf.o"])
for seat in ("bios", "uefi"):
    require_objects(f"{seat} tlsf", actual[seat], ["tlsf.o"])
# Hosted TLSF and wasmmod C are compiled by wasmmod's Cargo build script into
# hash-keyed archives. Require both source paths in the generated dep-info and
# the resulting Metal archive rather than guessing Cargo's hash directory.
host_cargo_d = METAL / "target" / "release" / "libpymergetic_metal.d"
if not host_cargo_d.is_file() or "extmod/wasmmod/src/" not in host_cargo_d.read_text():
    fail("host wasmmod Rust dep-info absent")
if not any((METAL / "target" / "release" / "build").rglob("libpm_util_mem.a")):
    fail("host TLSF/util.mem archive absent")
print("external seat check: host TLSF and wasmmod Cargo archives present")

# Hosted WAMR is selected by its CMake configuration (interp + AOT), unlike the
# deliberate freestanding subset below. Its compiler-emitted depfiles are the
# exact configured source ledger; require every primary source and both engines.
wamr_depfiles = list((METAL / "target" / "release" / "build").rglob(
    "vmlib/build/CMakeFiles/vmlib.dir/**/*.o.d"))
wamr_sources = set()
for depfile in wamr_depfiles:
    first = depfile.read_text().replace("\\\n", " ").split(":", 1)
    if len(first) != 2:
        fail(f"host WAMR malformed depfile {depfile}")
    candidates = [word for word in first[1].split() if word.endswith((".c", ".S", ".s"))]
    if candidates:
        source = Path(candidates[0])
        if not source.is_file():
            fail(f"host WAMR source missing: {source}")
        wamr_sources.add(str(source))
if len(wamr_sources) < 25 or not any("/interpreter/" in x for x in wamr_sources) \
        or not any("/aot/" in x for x in wamr_sources):
    fail("host WAMR configured source ledger lacks interpreter or AOT engine")
print(f"external seat check: host/unix WAMR: {len(wamr_sources)} configured sources compiled")

# MicroPython core: py.mk is the source authority. Every seat must carry every
# core object; the host embed package additionally proves generated source→obj.
py_words = make_words(TOP / "py" / "py.mk", "PY_CORE_O_BASENAME")
py_core = ["py/" + Path(word).name for word in py_words]
for seat in ("unix", "browser", "bios", "uefi"):
    require_objects(f"{seat} micropython", actual[seat], py_core)
embed_src = METAL / "build" / "upy-embed" / "micropython_embed"
embed_obj = METAL / "build" / "upy-embed" / "obj"
embed_expected = [path.relative_to(embed_src).with_suffix(".o") for path in embed_src.rglob("*.c")]
embed_missing = [str(path) for path in embed_expected if not (embed_obj / path).is_file()]
if embed_missing:
    fail(f"host micropython: generated sources without objects: {', '.join(embed_missing[:8])}")
print(f"external seat check: host micropython: {len(embed_expected)} generated sources compiled")

# WAMR freestanding list is shared by browser/BIOS/UEFI. Require every common
# declared source object; each architecture adds its deliberate trampoline.
wamr_mk = WASMMOD / "ports" / "freestanding" / "wamr_freestanding.mk"
wamr_src = make_words(wamr_mk, "SRCS")
def wamr_obj(source: str) -> str:
    rel = source.replace("$(SHARED)/", "core/shared/").replace("$(IWASM)/", "core/iwasm/")
    return "wamr/obj/" + rel.replace("/", "_").rsplit(".", 1)[0] + ".c.o"
wamr_common = [wamr_obj(source) for source in wamr_src]
for seat in ("browser", "bios", "uefi"):
    require_objects(f"{seat} wamr", actual[seat], wamr_common)
require_objects("browser WAMR adapters", actual["browser"], [
    "wamr/obj/core_iwasm_common_arch_invokeNative_general.c.o",
    "extmod/wasmmod/ports/webassembly/wamr/platform.o",
])
require_objects("BIOS WAMR trampoline", actual["bios"], [
    "wamr/obj/core_iwasm_common_arch_invokeNative_em64.s.o",
])
require_objects("UEFI WAMR trampoline", actual["uefi"], [
    "wamr/obj/invokeNative_win64_nosse.o",
])


# Unix uses the hosted Cargo archive plus the explicit C bridge list from the
# non-browser branch of wasmmod's MicroPython integration.
unix_wm = make_words(WASMMOD / "ports" / "micropython" / "micropython.mk",
                         "SRC_WASMMOD", occurrence=2)
unix_expected = [source.replace("$(WASMMOD_DIR)/../../", "")
                 .replace("$(WASMMOD_DIR)/", "extmod/wasmmod/")
                 for source in source_objects(unix_wm)]
require_objects("unix wasmmod C", actual["unix"], unix_expected)
require_objects("unix wasmmod optional C", actual["unix"], [
    "extmod/wasmmod/ports/micropython/modgen.o",
    "extmod/wasmmod/src/pymergetic/wasmmod/pack/format/elf/load.o",
])
if "extmod/wasmmod/src/" not in host_cargo_d.read_text():
    fail("unix wasmmod Rust sources absent from shared Cargo archive dep-info")
print("external seat check: unix wasmmod Rust uses tracked shared Cargo archive")

# C portions of wasmmod use explicit seat source lists. Rust portions emit
# dep-info; check both defining languages instead of substituting one for other.
browser_wm = make_words(WASMMOD / "ports" / "micropython" / "micropython.mk", "SRC_WASMMOD")
browser_expected = [source.replace("$(WASMMOD_DIR)/../../", "")
                    .replace("$(WASMMOD_DIR)/", "extmod/wasmmod/") for source in source_objects(browser_wm)]
require_objects("browser wasmmod C", actual["browser"], browser_expected)
fw_wm = make_words(METAL / "port" / "upy.mk", "SRC_UPY_WASMMOD")
fw_expected = source_objects(fw_wm)
for seat in ("bios", "uefi"):
    require_objects(f"{seat} wasmmod C", actual[seat], fw_expected)
rust_graphs = {}
for seat, root in (("browser", browser), ("bios", bios), ("uefi", uefi)):
    depfile = root / "libfw_lock.d"
    rust_dependencies(depfile, ("wasmmod/src/", "wasmmod\\src\\"), f"{seat} wasmmod Rust")
    data = depfile.read_text().replace("\\\n", " ")
    graph = set()
    for token in data.split():
        if not token.endswith(".rs") or "wasmmod/" not in token:
            continue
        graph.add(token.split("wasmmod/", 1)[1].replace("/../", "/"))
    rust_graphs[seat] = graph
if not (rust_graphs["browser"] == rust_graphs["bios"] == rust_graphs["uefi"]):
    fail("browser/BIOS/UEFI no-std wasmmod Rust graphs differ")
print("external seat check: no-std wasmmod Rust graphs match across seats")

for artifact in (browser / "micropython.wasm", bios / "metal.elf",
                 uefi / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI"):
    if not artifact.is_file() or artifact.stat().st_size == 0:
        fail(f"final linked seat artifact absent: {artifact}")
print("external seat check: browser/BIOS/UEFI final linked artifacts present")

print("external seat check: all declared supported source sets compiled and linked")
