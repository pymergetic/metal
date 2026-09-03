#!/usr/bin/env python3
"""Deterministically assemble the rsx compiler's `__impl__.rs` from parts/.

The micro-rustc compiler card is authored as focused fragments under
`parts/` (lexer, AST/parser, tables, lowering passes, root) and assembled
into the card's single `__impl__.rs` TU. rsx compiles a card standalone —
one `.rs` in, one C unit out; `mod name;` lowers to a comment — so the
compiler's muscle stays ONE translation unit and the split is physical,
not semantic: parts are concatenated in the fixed order below, byte for
byte, no reformatting, no edits.

The assembled `__impl__.rs` is byte-identical to the pre-split file (the
split was mechanical: every fragment is an exact span of the original).
That identity is the phase's fixed-point claim: the self-host loop, the
embedded source pane, ksweep and every seat consume the same bytes they
consumed before the split.

Order is the tool's, not the filesystem's: `parts/00-head.rs` first (module
docs + ABI mirrors + byte helpers), then lexer, parser, tables, the Lower
passes, the AST dump, and the C ABI entry points last. A part that forgets
its trailing newline, or a hand-edit to `__impl__.rs` that a part does not
carry, is a build failure here — never silent drift.

Run from anywhere; paths resolve from this file's location:

    python3 tools/rsx_assemble.py            # write when bytes differ
    python3 tools/rsx_assemble.py --check    # exit 1 on drift

The output is left untouched when identical (same posture as embed_src.py,
so a clean tree does not recompile every consumer).
"""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import sys

TOOLS = pathlib.Path(__file__).resolve().parent
CARD = TOOLS.parent / "src" / "pymergetic" / "metal" / "jit" / "rs" / "compiler"
PARTS = CARD / "parts"
OUT = CARD / "__impl__.rs"

# The assembly order. Names are the single source of truth; adding a part
# means adding it here (a part on disk that this list omits is an error,
# and so is a list entry with no part on disk).
ORDER = [
    "00-head.rs",       # module docs, C ABI mirrors, byte helpers
    "10-lexer.rs",      # Toks, Out, Lexer
    "20-parser.rs",     # Node, Kids, Parser
    "30-tables.rs",     # SymTab, FnTab, ConstTab, EnumTab, LocalTab
    "40-lower-core.rs", # Lower struct + table/type helpers
    "50-lower-stmt.rs", # statement/expression emitters
    "60-lower-expr.rs", # expression emitter
    "70-lower-item.rs", # item lowering + type ordering
    "80-lower-fn.rs",   # fn lowering + file driver
    "90-ast-dump.rs",   # AST dump (inspect face)
    "99-root.rs",       # C ABI entry points + registration
]


def assemble() -> bytes:
    out = bytearray()
    for i, name in enumerate(ORDER):
        path = PARTS / name
        if not path.is_file():
            print(f"rsx_assemble: missing part {name}", file=sys.stderr)
            sys.exit(1)
        data = path.read_bytes()
        if b"\x00" in data:
            print(f"rsx_assemble: {name} contains a NUL byte", file=sys.stderr)
            sys.exit(1)
        if not data.endswith(b"\n"):
            print(f"rsx_assemble: {name} does not end with a newline", file=sys.stderr)
            sys.exit(1)
        out += data
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="fail on drift instead of writing (never modifies)")
    ap.add_argument("--sha", action="store_true",
                    help="print the assembled sha256 and exit")
    args = ap.parse_args()

    on_disk = {p.name for p in PARTS.glob("*.rs")} if PARTS.is_dir() else set()
    unknown = sorted(on_disk - set(ORDER))
    if unknown:
        print(f"rsx_assemble: part(s) not in ORDER: {', '.join(unknown)}",
              file=sys.stderr)
        return 1

    data = assemble()
    if args.sha:
        print(hashlib.sha256(data).hexdigest())
        return 0

    if OUT.is_file() and OUT.read_bytes() == data:
        return 0

    if args.check:
        # --check is read-only by construction: the only write in this
        # tool lives below, past this branch.
        print("rsx_assemble: __impl__.rs is not the assembly of parts/ — drift",
              file=sys.stderr)
        return 1

    # Atomic write: build the new bytes in a sibling temp file, fsync,
    # then rename over __impl__.rs. A crash mid-write can never leave a
    # torn assembly — every seat that parses one line of rsx sees either
    # the whole old file or the whole new one.
    tmp = OUT.with_suffix(OUT.suffix + ".tmp")
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
