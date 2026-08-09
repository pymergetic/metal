#!/usr/bin/env python3
"""Host ledger test — MODULE_MATRIX claims vs tree + smoke lists.

Run:
  python3 tests/matrix/test_module_matrix_ledger.py
  make -C tests/matrix ledger
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from parse_matrix import (  # noqa: E402
    FROZEN_SEATS,
    METAL,
    MATRIX_MD,
    has_glue,
    has_pyi,
    manifest_has_frozen,
    parse_rows,
    parse_snapshot,
)

SMOKE_PY = METAL / "tests" / "py_smoke" / "matrix_import_all.py"
MAIN_UPY = METAL / "port" / "upy" / "main_upy.c"
MANIFEST_FW = METAL / "port" / "manifest.py"
MANIFEST_WASM = METAL / "port" / "manifest_wasm.py"
GLUE_SRC_MK = METAL / "port" / "glue_src.mk"
WASM_MK = METAL / "port" / "webassembly" / "variant" / "mpconfigvariant.mk"
BIOS64_MK = METAL / "port" / "boards" / "X86_64_BIOS" / "build.mk"
BOARD_MKS = (
    METAL / "port" / "boards" / "X86_64_BIOS" / "build.mk",
    METAL / "port" / "boards" / "X86_BIOS" / "build.mk",
    METAL / "port" / "boards" / "X86_64_UEFI" / "build.mk",
    METAL / "port" / "boards" / "X86_UEFI" / "build.mk",
)


def _paths_in_seats_tuple(text: str) -> list[str]:
    m = re.search(r"SEATS\s*=\s*\((.*?)\)", text, re.S)
    if not m:
        return []
    return [
        p
        for p in re.findall(r"""['"]([^'"]+)['"]""", m.group(1))
        if re.fullmatch(r"[a-z][a-z0-9_.]*", p)
    ]


def _paths_in_smoke_py(text: str) -> list[str]:
    return _paths_in_seats_tuple(text)


def _paths_in_main_upy(text: str) -> list[str]:
    return _paths_in_seats_tuple(text)


class ModuleMatrixLedger(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = MATRIX_MD.read_text(encoding="utf-8")
        cls.rows = parse_rows(cls.text)
        cls.snap = parse_snapshot(cls.text)

    def test_row_count_69(self):
        self.assertEqual(len(self.rows), 69)

    def test_paths_unique_sorted(self):
        paths = [r["path"] for r in self.rows]
        self.assertEqual(paths, sorted(paths))
        self.assertEqual(len(paths), len(set(paths)))

    def test_full_export_and_async_yes(self):
        bad = []
        for r in self.rows:
            if not (r["c"] == r["rs"] == r["py"] == r["api"]):
                bad.append(f"{r['path']}: export {r['c']}/{r['rs']}/{r['py']} vs api {r['api']}")
            if r["async"] != "yes":
                bad.append(f"{r['path']}: async={r['async']}")
        self.assertEqual(bad, [])

    def test_browser_fw_stub_yes(self):
        bad = [
            f"{r['path']}: Stub={r['stub']} Browser={r['browser']} FW={r['fw']}"
            for r in self.rows
            if r["stub"] != "yes" or r["browser"] != "yes" or r["fw"] != "yes"
        ]
        self.assertEqual(bad, [])

    def test_stub_pyi_exists(self):
        missing = [r["path"] for r in self.rows if r["stub"] == "yes" and not has_pyi(r["path"])]
        self.assertEqual(missing, [])

    def test_snapshot_matches_table(self):
        n = len(self.rows)
        full = sum(
            1
            for r in self.rows
            if r["c"] == r["rs"] == r["py"] == r["api"]
        )
        green = sum(
            1
            for r in self.rows
            if r["c"] == r["rs"] == r["py"] == r["api"] and r["async"] == "yes"
        )
        browser = sum(1 for r in self.rows if r["browser"] == "yes")
        fw = sum(1 for r in self.rows if r["fw"] == "yes")
        stub = sum(1 for r in self.rows if r["stub"] == "yes" and has_pyi(r["path"]))
        self.assertEqual(self.snap.get("Rows"), str(n))
        self.assertIn(f"{full}/{n}", self.snap.get("Full export (C∧RS∧Py @ 100%)", ""))
        self.assertIn(f"{green}/{n}", self.snap.get("Strict green (export ∧ async=yes)", ""))
        self.assertIn(f"{browser}/{n}", self.snap.get("Browser=yes", ""))
        self.assertIn(f"{fw}/{n}", self.snap.get("FW=yes", ""))
        self.assertIn(f"{stub}/{n}", self.snap.get("Stub=.pyi", ""))

    def test_browser_seat_wired(self):
        """Browser=yes ⇒ glue nest, or frozen in manifest_wasm, or wasm SRC bridge."""
        wasm_mk = WASM_MK.read_text(encoding="utf-8")
        bad = []
        for r in self.rows:
            if r["browser"] != "yes":
                continue
            p = r["path"]
            if p in FROZEN_SEATS:
                if not manifest_has_frozen(MANIFEST_WASM, p):
                    bad.append(f"{p}: missing frozen in manifest_wasm.py")
                continue
            if has_glue(p):
                # glue must be listed for qstr/link on wasm
                glue_tail = "/glue/pymergetic/metal/" + p.replace(".", "/")
                if glue_tail not in wasm_mk and glue_tail + ".c" not in wasm_mk:
                    # __init__.c parents use path/__init__.c
                    alt = glue_tail + ".c"
                    alt2 = glue_tail + "/__init__.c"
                    if alt not in wasm_mk and alt2.replace(str(METAL), "$(METAL)") not in wasm_mk:
                        # mpconfig uses $(METAL)/glue/...
                        key = "glue/pymergetic/metal/" + p.replace(".", "/")
                        if key not in wasm_mk:
                            bad.append(f"{p}: glue exists but not in mpconfigvariant.mk")
                continue
            bad.append(f"{p}: no glue nest and not a frozen seat")
        self.assertEqual(bad, [])

    def test_fw_seat_wired(self):
        """FW=yes ⇒ glue_src.mk and/or frozen manifest.py and/or board bridge OBJ."""
        glue_mk = GLUE_SRC_MK.read_text(encoding="utf-8")
        bios = BIOS64_MK.read_text(encoding="utf-8")
        bad = []
        for r in self.rows:
            if r["fw"] != "yes":
                continue
            p = r["path"]
            if p in FROZEN_SEATS:
                if not manifest_has_frozen(MANIFEST_FW, p):
                    bad.append(f"{p}: missing frozen in manifest.py")
                # unix / arch bridges on board
                if p.startswith("unix.") and "metal_unix_" not in bios:
                    bad.append(f"{p}: board missing metal_unix_*.o")
                continue
            key = "pymergetic/metal/" + p.replace(".", "/")
            if key + ".c" in glue_mk or key + "/__init__.c" in glue_mk:
                continue
            if has_glue(p):
                bad.append(f"{p}: glue file exists but not in glue_src.mk")
            else:
                bad.append(f"{p}: no FW glue/frozen wiring")
        self.assertEqual(bad, [])

    def test_all_boards_link_abi_faces(self):
        """Glue nests for stub seats need abi_faces_link on every FW board (not only BIOS64)."""
        missing = [
            str(mk.relative_to(METAL))
            for mk in BOARD_MKS
            if "metal_abi_faces.o" not in mk.read_text(encoding="utf-8")
            or "abi_faces_link.c" not in mk.read_text(encoding="utf-8")
        ]
        self.assertEqual(missing, [])

    def test_smoke_py_lists_all_seats(self):
        text = SMOKE_PY.read_text(encoding="utf-8")
        got = _paths_in_smoke_py(text)
        want = [r["path"] for r in self.rows]
        self.assertEqual(got, want)

    def test_main_upy_lists_all_seats(self):
        text = MAIN_UPY.read_text(encoding="utf-8")
        got = _paths_in_main_upy(text)
        want = [r["path"] for r in self.rows]
        self.assertEqual(got, want)
        self.assertIn("matrix py ok", text)
        self.assertIn("matrix py ok", BIOS64_MK.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
