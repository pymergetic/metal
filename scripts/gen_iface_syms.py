#!/usr/bin/env python3
"""
Scrape every registered NativeSymbol[] table across src/pymergetic/metal/**
into one flat sym table (docs/DOC_IFACE_PLAN.md Part II-B) — the reflection
side (module + name + WAMR sig) of the iface catalog, never a second
hand-written signature list.

For each `static NativeSymbol <arr>[] = { { "name", (void*)fn, "sig", NULL },
... };` block, this pairs it with the same file's own
`wasm_runtime_register_natives(<MODULE_MACRO>, <arr>, ...)` call and resolves
<MODULE_MACRO> to its string literal by scanning every header under
include/pymergetic/metal for that macro's own `#define`.

Optional doc_key merge: scripts/iface_doc_keys.txt, one
"<module> <name> <doc_key>" line per row (whitespace-separated, '#' comments
allowed) — a pointer into util/doc.c's catalog (Part I), never pasted text.

Output: src/pymergetic/metal/util/iface_syms.inc.c (generated — regenerated
in place by every build, see that file's own header; committed with a
0-row placeholder so clangd/one-off compiles work before the first build,
same pattern as guest/wasm/embed_mods.inc.c).

Usage: scripts/gen_iface_syms.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_METAL = ROOT / "src" / "pymergetic" / "metal"
INCLUDE_METAL = ROOT / "include" / "pymergetic" / "metal"
DOC_KEYS_FILE = ROOT / "scripts" / "iface_doc_keys.txt"
OUT = SRC_METAL / "util" / "iface_syms.inc.c"

MACRO_DEFINE_RE = re.compile(r'#define\s+(PM_METAL_\w+_WASI_MODULE)\s+"([^"]+)"')
NATIVE_ARRAY_RE = re.compile(
    r"static\s+NativeSymbol\s+(\w+)\s*\[\]\s*=\s*\{(?P<body>.*?)\n\};", re.DOTALL
)
# Two row shapes seen in this tree: the plain WAMR literal
# `{ "name", (void*)fn, "sig", NULL }` (most modules), and a per-module
# wrapper macro `SOME_NATIVE("name", "sig", fn)` (fs.c's PM_METAL_FS_NATIVE,
# used so its long async-name/sig/fn triples line up column-wise).
NATIVE_ROW_RE = re.compile(
    r'\{\s*"(?P<name1>[^"]+)"\s*,\s*\([^)]*\)\s*\w+\s*,\s*"(?P<sig1>[^"]*)"\s*,\s*NULL\s*\}'
    r'|\w+\(\s*"(?P<name2>[^"]+)"\s*,\s*"(?P<sig2>[^"]*)"\s*,\s*\w+\s*\)'
)
REGISTER_CALL_RE = re.compile(r"wasm_runtime_register_natives\(\s*([A-Za-z0-9_\"]+)\s*,")


def load_module_macros() -> dict[str, str]:
    macros: dict[str, str] = {}
    for f in INCLUDE_METAL.rglob("*.h"):
        text = f.read_text(errors="replace")
        for m in MACRO_DEFINE_RE.finditer(text):
            macros[m.group(1)] = m.group(2)
    return macros


def iter_c_files():
    for f in sorted(SRC_METAL.rglob("*.c")):
        if f.name == "iface_syms.inc.c":
            continue
        yield f


def load_doc_keys() -> dict[tuple[str, str], str]:
    out: dict[tuple[str, str], str] = {}
    if not DOC_KEYS_FILE.exists():
        return out
    for line in DOC_KEYS_FILE.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 3:
            print(f"gen_iface_syms: skip malformed line: {line!r}", file=sys.stderr)
            continue
        module, name, doc_key = parts
        out[(module, name)] = doc_key
    return out


def resolve_module(raw: str, macros: dict[str, str]) -> str | None:
    raw = raw.strip()
    if raw.startswith('"'):
        return raw.strip('"')
    return macros.get(raw)


def scan() -> list[tuple[str, str, str]]:
    macros = load_module_macros()
    rows: list[tuple[str, str, str]] = []

    for f in iter_c_files():
        text = f.read_text(errors="replace")
        arrays = list(NATIVE_ARRAY_RE.finditer(text))
        if not arrays:
            continue

        calls = list(REGISTER_CALL_RE.finditer(text))
        if not calls:
            print(f"gen_iface_syms: {f}: NativeSymbol array with no register call, skipped", file=sys.stderr)
            continue

        # One array, one call is the overwhelmingly common shape (every
        # module in this tree today) — pair the first of each; a file
        # with more than one of either would need a smarter pairing this
        # script doesn't attempt (none exist as of this writing).
        module = resolve_module(calls[0].group(1), macros)
        if module is None:
            print(f"gen_iface_syms: {f}: unresolved module macro {calls[0].group(1)!r}, skipped", file=sys.stderr)
            continue

        for arr_m in arrays:
            for row_m in NATIVE_ROW_RE.finditer(arr_m.group("body")):
                name = row_m.group("name1") or row_m.group("name2")
                sig = row_m.group("sig1") if row_m.group("name1") else row_m.group("sig2")
                rows.append((module, name, sig))

    return rows


def c_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def render(rows: list[tuple[str, str, str]], doc_keys: dict[tuple[str, str], str]) -> str:
    # Include the type header so clangd / one-file -fsyntax-only on this
    # generated .inc.c (PathMatch treats *.inc.c as a TU) can resolve
    # pm_metal_iface_sym_t. When pulled into iface.c the include guard
    # makes the second include a no-op — same shape as embed_mods.inc.c
    # carrying its own typedef, just shared through iface.h instead.
    lines = [
        "/* AUTO-GENERATED by scripts/gen_iface_syms.py — do not hand-edit */",
        "#include <stdint.h>",
        "#include <pymergetic/metal/util/iface.h>",
        "",
    ]
    lines.append("static const pm_metal_iface_sym_t g_pm_metal_iface_syms[] = {")
    for module, name, sig in sorted(rows):
        doc_key = doc_keys.get((module, name), "")
        lines.append(
            f'  {{ "{c_escape(module)}", "{c_escape(name)}", "{c_escape(sig)}", 0, "{c_escape(doc_key)}" }},'
        )
    lines.append("};")
    lines.append("")
    lines.append(f"static const uint32_t g_pm_metal_iface_sym_count = {len(rows)}u;")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    doc_keys = load_doc_keys()
    rows = scan()
    OUT.write_text(render(rows, doc_keys))
    print(f"gen_iface_syms: wrote {OUT} ({len(rows)} native symbols, {len(doc_keys)} doc_key overrides)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
