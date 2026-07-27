#!/usr/bin/env python3
"""Precompile mods/py/templates/*.html to *_html.py for utemplate.compiled.Loader.

Guest FAT is awkward for on-the-fly compile (source.Loader writes .py next to
the template). Run this on the host whenever a .html template changes; the
launcher uses compiled.Loader only.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TPL_DIR = ROOT / "templates"
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "microdot_src" / "libs" / "common"))

from utemplate.source import Compiler  # noqa: E402


def main() -> int:
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
        print("compile_templates:", name, "->", out_name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
