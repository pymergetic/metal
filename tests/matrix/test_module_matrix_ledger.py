#!/usr/bin/env python3
"""Host checks — reg seats SoT + wiring; MODULE_MATRIX is export-only.

Run:
  python3 tests/matrix/test_module_matrix_ledger.py
  make -C tests/matrix ledger
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from parse_matrix import (  # noqa: E402
    FROZEN_SEATS,
    METAL,
    MATRIX_MD,
    SEATS_C,
    has_glue,
    has_pyi,
    manifest_has_frozen,
    parse_reg_seats,
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


class RegSeatsAuthority(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.seats = parse_reg_seats()
        cls.text = MATRIX_MD.read_text(encoding="utf-8")
        cls.rows = parse_rows(cls.text)
        cls.snap = parse_snapshot(cls.text)

    def test_seats_c_exists_and_sorted(self):
        self.assertTrue(SEATS_C.is_file())
        seats_src = SEATS_C.read_text(encoding="utf-8")
        self.assertNotIn("floor[]", seats_src)
        self.assertNotIn("PM_METAL_REG_SEAT_CAP", seats_src)
        self.assertNotIn("g_seats[", seats_src)
        self.assertIn("pm_metal_reg_seat_splice", seats_src)
        self.assertEqual(len(self.seats), 70)
        self.assertEqual(self.seats, sorted(self.seats))
        self.assertEqual(len(self.seats), len(set(self.seats)))

    def test_no_do_str_in_main_upy(self):
        text = MAIN_UPY.read_text(encoding="utf-8")
        self.assertNotIn("do_str(", text)
        self.assertNotIn("SEATS=", text)
        self.assertIn("pm_metal_reg_run_tests", text)
        self.assertIn("pm_metal_reg_seats_boot", text)

    def test_smoke_py_uses_reg(self):
        text = SMOKE_PY.read_text(encoding="utf-8")
        self.assertIn("reg.seat_count", text)
        self.assertIn("reg.seat_at", text)
        self.assertNotIn("SEATS = (", text)

    def test_boards_grep_reg_seats_ok(self):
        for mk in BOARD_MKS:
            text = mk.read_text(encoding="utf-8")
            self.assertIn("reg seats ok", text, mk)
            self.assertIn("metal_reg_seats.o", text, mk)
            self.assertNotIn("matrix py ok", text, mk)

    def test_matrix_md_demoted(self):
        self.assertIn("Not the source of truth", self.text)

    def test_row_count_matches_reg_when_present(self):
        """Export table may lag; when present it should list the same paths."""
        if not self.rows:
            return
        self.assertEqual([r["path"] for r in self.rows], self.seats)

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
        missing = [p for p in self.seats if not has_pyi(p)]
        self.assertEqual(missing, [])

    def test_snapshot_matches_table(self):
        n = len(self.rows)
        if n == 0:
            return
        full = sum(1 for r in self.rows if r["c"] == r["rs"] == r["py"] == r["api"])
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
        """Browser seats ⇒ glue nest, or frozen in manifest_wasm, or wasm SRC bridge."""
        wasm_mk = WASM_MK.read_text(encoding="utf-8")
        bad = []
        for p in self.seats:
            if p in FROZEN_SEATS:
                if not manifest_has_frozen(MANIFEST_WASM, p):
                    bad.append(f"{p}: missing frozen in manifest_wasm.py")
                continue
            if has_glue(p):
                key = "glue/pymergetic/metal/" + p.replace(".", "/")
                if key not in wasm_mk:
                    bad.append(f"{p}: glue exists but not in mpconfigvariant.mk")
                continue
            bad.append(f"{p}: no glue nest and not a frozen seat")
        self.assertEqual(bad, [])

    def test_fw_seat_wired(self):
        """FW seats ⇒ glue_src.mk and/or frozen manifest.py and/or board bridge OBJ."""
        glue_mk = GLUE_SRC_MK.read_text(encoding="utf-8")
        bios = BIOS64_MK.read_text(encoding="utf-8")
        bad = []
        for p in self.seats:
            if p in FROZEN_SEATS:
                if not manifest_has_frozen(MANIFEST_FW, p):
                    bad.append(f"{p}: missing frozen in manifest.py")
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

    def test_forbid_abi_faces_dump(self):
        dump = METAL / "src" / "pymergetic" / "metal" / "port" / "abi_faces_link.c"
        self.assertFalse(dump.exists(), "abi_faces_link.c must not exist")
        offenders = []
        for mk in BOARD_MKS + (WASM_MK,):
            text = mk.read_text(encoding="utf-8")
            if "abi_faces_link.c" in text or "metal_abi_faces.o" in text:
                offenders.append(str(mk.relative_to(METAL)))
        self.assertEqual(offenders, [])

    def test_forbid_host_natives_stub(self):
        stub = (
            METAL
            / "src"
            / "pymergetic"
            / "metal"
            / "wamr_host"
            / "port"
            / "host_natives_stub.c"
        )
        self.assertFalse(stub.exists(), "host_natives_stub.c must not exist")
        fill = METAL / "src" / "pymergetic" / "metal" / "py" / "port" / "upy_io_fill.c"
        self.assertTrue(fill.is_file(), "upy_io_fill.c must exist")
        need = (
            "metal_wamr_host_natives.o",
            "metal_wamr_wasi_stubs.o",
            "upy_io_fill.o",
            "host_natives.c",
        )
        forbid = ("host_natives_stub", "metal_wamr_host_natives_stub")
        missing = []
        offenders = []
        for mk in BOARD_MKS:
            text = mk.read_text(encoding="utf-8")
            for token in need:
                if token not in text:
                    missing.append(f"{mk.relative_to(METAL)}:{token}")
            for token in forbid:
                if token in text:
                    offenders.append(f"{mk.relative_to(METAL)}:{token}")
        self.assertEqual(missing, [])
        self.assertEqual(offenders, [])

    def test_quiesce_lives_in_async_crate(self):
        async_rs = (METAL / "src" / "pymergetic" / "metal" / "async" / "__init__.rs").read_text(
            encoding="utf-8"
        )
        self.assertIn("mod quiesce", async_rs)
        kernel = (METAL / "src" / "pymergetic" / "metal" / "reg" / "_kernel.rs").read_text(
            encoding="utf-8"
        )
        self.assertNotIn('#[path = "../async/quiesce.rs"]', kernel)
        browser_toml = (
            METAL / "crates" / "pymergetic_metal_browser_rs" / "Cargo.toml"
        ).read_text(encoding="utf-8")
        self.assertIn("pymergetic_metal_async", browser_toml)
        extra = (
            METAL / "port" / "webassembly" / "variant" / "extra_src.mk"
        ).read_text(encoding="utf-8")
        self.assertIn("async/quiesce.rs", extra)

    def test_cold_reg_ledger_present(self):
        ledger = METAL / "src" / "pymergetic" / "metal" / "reg" / "_ledger.rs"
        self.assertTrue(ledger.is_file())
        hdr = METAL / "include" / "pymergetic" / "metal" / "reg" / "ledger.h"
        self.assertTrue(hdr.is_file())
        seats_h = METAL / "include" / "pymergetic" / "metal" / "reg" / "seats.h"
        self.assertTrue(seats_h.is_file())
        glue = (METAL / "port" / "glue_src.mk").read_text(encoding="utf-8")
        self.assertIn("pymergetic/metal/reg.c", glue)
        stubs = (
            METAL / "src" / "pymergetic" / "metal" / "inspect" / "stubs.py"
        ).read_text(encoding="utf-8")
        self.assertIn("/inspect/reg", stubs)
        self.assertIn("/inspect/reg/seats", stubs)
        adapter = (
            METAL / "src" / "pymergetic" / "metal" / "inspect" / "adapter_microdot.py"
        ).read_text(encoding="utf-8")
        self.assertIn("ledger_json", adapter)
        self.assertIn("seats_json", adapter)
        qstr = (METAL / "port" / "upy" / "qstrdefsport.h").read_text(encoding="utf-8")
        self.assertIn("Q(pymergetic.metal.reg)", qstr)
        self.assertIn("Q(seat_count)", qstr)
        nest = (METAL / "glue" / "pymergetic" / "metal" / "__init__.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("mp_module_pymergetic_metal_reg", nest)
        wasm = WASM_MK.read_text(encoding="utf-8")
        self.assertIn("pymergetic/metal/reg.c", wasm)
        self.assertIn("reg/seats.c", wasm)

    def test_cold_ledger_honesty_and_async_parity(self):
        seed = (METAL / "src" / "pymergetic" / "metal" / "reg" / "__init__.rs").read_text(
            encoding="utf-8"
        )
        self.assertIn("HONESTY_INCOMPLETE", seed)
        self.assertIn("HONESTY_STUB", seed)
        self.assertIn("client_later", seed)
        self.assertIn("browser_hal_stub", seed)
        self.assertIn("rs_hal_face", seed)
        self.assertIn("py_hal_face", seed)
        ledger = (METAL / "src" / "pymergetic" / "metal" / "reg" / "_ledger.rs").read_text(
            encoding="utf-8"
        )
        self.assertIn("sync_muscle_without_async_partner_is_gap", ledger)
        self.assertIn("honesty_stub_counts_as_gap", ledger)
        self.assertIn("async_partner", ledger)
        kernel = (METAL / "src" / "pymergetic" / "metal" / "reg" / "_kernel.rs").read_text(
            encoding="utf-8"
        )
        self.assertIn("publish_entries_to_ledger", kernel)
        self.assertIn("VIA_IMPORT_ROW", kernel)
        self.assertIn("pm_metal_reg_seat_on_mod_load", kernel)

    def test_boards_link_stream_blk_muscle(self):
        need = (
            "metal_stream.o",
            "metal_blk_detect.o",
            "metal_virtio_blk.o",
            "dev/stream/__init__.c",
            "dev/blk/_detect.c",
            "dev/blk/_virtio_blk.c",
        )
        missing = []
        for mk in BOARD_MKS:
            text = mk.read_text(encoding="utf-8")
            for token in need:
                if token not in text:
                    missing.append(f"{mk.relative_to(METAL)}:{token}")
        self.assertEqual(missing, [])
        wasm = WASM_MK.read_text(encoding="utf-8")
        self.assertIn("dev/stream/__init__.c", wasm)
        self.assertIn("hal/wasm/dev_blk.c", wasm)
        self.assertNotIn("dev/blk/_virtio_blk.c", wasm)


if __name__ == "__main__":
    unittest.main(verbosity=2)
