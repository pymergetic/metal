#!/usr/bin/env python3
"""Assemble the rsx compiler's flat TU from parts/ — no tree output.

The micro-rustc card is authored as focused fragments under `parts/`
(lexer, AST/parser, tables, lowering passes, root). The parts are spans
of ONE translation unit, not modules — `impl` blocks straddle part
boundaries by design — so the flat TU they form is assembled, and every
consumer gets the same bytes:

  - the kernel's rsx (ksweep, the in-kernel self-host) never reads a flat
    file: the build card's `#[path]` splice appends each part after the
    card's shim (`__impl__.rs`), in the same ORDER — byte-identical to
    this assembly, proven by ksweep and the self-host fixed point;
  - cargo compiles the flat TU that `build.rs` writes into OUT_DIR (same
    ORDER, same bytes; `compiler.rs` includes it);
  - tools/selfhost_cycle.sh assembles it into its work dir for the boot
    and self feeds;
  - tools/embed_src.py embeds the card's authored files (shim + parts).

Nothing in this tool writes into the committed tree: the generated
monolith that used to sit at `__impl__.rs` is gone — the parts ARE the
card, and this tool is the reference assembler (build.rs ports it for
cargo; `--check-flat` cross-checks the port against this one).

Run from anywhere; paths resolve from this file's location:

    python3 tools/rsx_assemble.py                 # flat TU to stdout
    python3 tools/rsx_assemble.py -o FILE         # ... or to a file
    python3 tools/rsx_assemble.py --check-flat F  # F == assembly?
    python3 tools/rsx_assemble.py --sha           # print sha256, exit
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import sys

TOOLS = pathlib.Path(__file__).resolve().parent
CARD = TOOLS.parent / "src" / "pymergetic" / "metal" / "jit" / "rs" / "compiler"
PARTS = CARD / "parts"

# The assembly order. Names are the single source of truth; adding a part
# means adding it here (a part on disk that this list omits is an error,
# and so is a list entry with no part on disk). build.rs and the card's
# `mod` shim carry the same order — the three must stay in lockstep.
ORDER = [
    "head.rs",          # module docs, C ABI mirrors, byte helpers
    "lexer.rs",         # Toks, Out, Lexer
    "parser.rs",        # Node, Kids, Parser
    "tables.rs",        # SymTab, FnTab, ConstTab, EnumTab, LocalTab
    "lower_core.rs",    # Lower struct + table/type helpers
    "lower_stmt.rs",    # statement/expression emitters
    "lower_expr.rs",    # expression emitter
    "lower_item.rs",    # item lowering + type ordering
    "lower_fn.rs",      # fn lowering + file driver
    "ast_dump.rs",      # AST dump (inspect face)
    "root.rs",          # C ABI entry points + registration
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
            print(f"rsx_assemble: {name} does not end with a newline",
                  file=sys.stderr)
            sys.exit(1)
        out += data
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--out", metavar="FILE",
                    help="write the flat TU here (default: stdout)")
    ap.add_argument("--check-flat", metavar="FILE",
                    help="exit 1 unless FILE is byte-identical to the assembly")
    ap.add_argument("--sha", action="store_true",
                    help="print the assembled sha256 and exit")
    ap.add_argument("--quiet", "-q", action="store_true",
                    help="assemble for validity only: no output on success")
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

    if args.check_flat is not None:
        flat = pathlib.Path(args.check_flat)
        if not flat.is_file():
            print(f"rsx_assemble: no flat file at {flat}", file=sys.stderr)
            return 1
        if flat.read_bytes() != data:
            print(f"rsx_assemble: {flat} is not the assembly of parts/ — drift",
                  file=sys.stderr)
            return 1
        return 0

    if args.out is not None:
        out = pathlib.Path(args.out)
        # Atomic write: build the bytes in a sibling temp file, fsync,
        # then rename over the target. A crash mid-write can never leave
        # a torn assembly — every consumer sees either the whole old
        # file or the whole new one.
        tmp = out.with_suffix(out.suffix + ".tmp")
        with open(tmp, "wb") as f:
            f.write(data)
            f.flush()
            import os
            os.fsync(f.fileno())
        import os
        os.replace(tmp, out)
        return 0

    if args.quiet:
        return 0

    sys.stdout.buffer.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
