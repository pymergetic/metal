#!/usr/bin/env python3
"""Precompile mods/api/templates/*.html to *_html.py for utemplate.compiled.Loader."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
METAL = ROOT.parent.parent
TPL_DIR = ROOT / "templates"
# Host checkout: external/utemplate/*.py (gitignored). Guest: /mods/utemplate/.
sys.path.insert(0, str(METAL / "external"))

from utemplate.source import Compiler  # noqa: E402


def main() -> int:
    # Host CPython env — typings/os.pyi is Metal's guest os (no environ).
    quiet = getattr(__import__("os"), "environ", {}).get("PM_METAL_STAGE_QUIET") == "1"
    names = sorted(p.name for p in TPL_DIR.glob("*.html"))
    if not names:
        print("compile_templates: no .html under", TPL_DIR, file=sys.stderr)
        return 1

    class DirLoader:
        def input_open(self, name: str):
            return open(TPL_DIR / name, encoding="utf-8")

    loader = DirLoader()
    for name in names:
        out_name = name.replace(".", "_") + ".py"
        out_path = TPL_DIR / out_name
        with open(TPL_DIR / name, encoding="utf-8") as fin, open(
            out_path, "w", encoding="utf-8"
        ) as fout:
            Compiler(fin, fout, loader=loader).compile()
        if not quiet:
            print("compile_templates:", name, "->", out_name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
