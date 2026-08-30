#!/usr/bin/env python3
"""Embed each card's muscle source (.c/.rs) into a C include for the seat.

The Inspect commander's source pane is real code, not a stub: every card that
registers a C/Rust export on the seat (`impl = "c"` / `impl = "rs"` with a
`__impl__.*` muscle) ships its actual source so the UI can show and serve it on
every seat — including firmware, which cannot fopen a file. The embed is bytes
in the image, exactly like www_embed.inc.h for the HTTP assets; there is no
second copy of the source anywhere (edit the .c/.rs, not this output).

Discovery is the card tree: a directory holding __pmm__.toml is a card, and its
`fqn` field is the authoritative registry FQN (pymergetic.util.gen's
find_cards() reads the same field). Only cards with a muscle (`__impl__.c` /
`__impl__.rs`) get a source slot — `impl = "py"` cards have no native source to
browse. Companion .c/.rs units beside the muscle (net/wg's __crypto__.c, e.g.)
ride along; generated faces (__exports__.* / __types__.*) and prove tests are
skipped, they are not authored source.

Output is a header that both the C inspect card and the .rs driver include:

    static const pm_metal_src_file_t PM_METAL_SRC_FILES_<n>[] = {...};
    static const pm_metal_src_card_t PM_METAL_SRC_CARDS[] = {
        { .fqn="...", .impl="c", .files=..., .nfiles=..., .manifest=..., .toml=... },
        ...
    };
    uint32_t pm_metal_src_card_count(void);
    const pm_metal_src_card_t *pm_metal_src_find(const char *fqn);

`impl` is the manifest's impl string and `toml` the raw __pmm__.toml bytes
(NUL-terminated) — the build card's runtime discovery parses them to
synthesize build units without touching the filesystem.

pm_metal_src_find is a plain binary search over the sorted card table. The
generator leaves the output header untouched when it is byte-identical, so a
no-information [re]build does not recompile every card that includes it.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
import tomllib


def _emit_array(out, name: str, data: bytes) -> None:
    out.write(f"static const uint8_t s_src_{name}[] = {{\n")
    if not data:
        out.write("    0\n")
    else:
        for i in range(0, len(data), 16):
            chunk = data[i : i + 16]
            out.write("    " + ", ".join(str(b) for b in chunk) + ("," if i + 16 < len(data) else "") + "\n")
    out.write("};\n\n")


def _ident(*parts: str) -> str:
    s = re.sub(r"[^A-Za-z0-9]+", "_", "_".join(parts)).strip("_")
    return s


def _read_card_meta(card_dir: pathlib.Path) -> tuple[str | None, str]:
    """(fqn, impl) from a card's __pmm__.toml, or (None, "") for a bad manifest."""
    toml = card_dir / "__pmm__.toml"
    if not toml.is_file():
        return None, ""
    try:
        with toml.open("rb") as f:
            doc = tomllib.load(f)
    except Exception as exc:  # noqa: BLE001 — a bad manifest must not kill the build silently
        print(f"embed_src: cannot parse {toml}: {exc}", file=sys.stderr)
        return None, ""
    fqn = doc.get("fqn")
    impl = doc.get("impl")
    return (fqn if isinstance(fqn, str) and fqn else None,
            impl if isinstance(impl, str) else "")


def _is_face(path: pathlib.Path) -> bool:
    """Generated faces / prove tests / crate glue are not authored muscle source."""
    name = path.name
    if name in ("__tests__.c", "__tests__.rs"):
        return True
    if name.startswith("__exports__") or name.startswith("__types__"):
        return True
    if name in ("face.py", "__init__.py", "__init__.pyi"):
        return True
    return False


def _muscle_files(card_dir: pathlib.Path, impl: str) -> list[pathlib.Path]:
    """The card's authored muscle source, minus faces, tests and crate glue.

    A C card owns only .c units (its muscle + companions like __tcp__.c); any
    .rs beside it is the cargo crate's module-declaration glue (`#[path=...] pub
    mod`), never card source, so it is skipped. An RS card owns __impl__.rs plus
    any authored companion .rs (util/gen's cli.rs/discover.rs/host.rs/sink.rs).
    """
    want = ".c" if impl == "c" else ".rs"
    out = []
    for p in sorted(card_dir.iterdir()):
        if not p.is_file() or p.suffix != want:
            continue
        if _is_face(p):
            continue
        out.append(p)
    # Always lead with the muscle (the file people actually click).
    impl_file = [p for p in out if p.name.startswith("__impl__")]
    rest = [p for p in out if not p.name.startswith("__impl__")]
    return impl_file + rest


def _card_manifest(fqn: str, files: list[tuple[str, int]]) -> str:
    tree = {
        "name": fqn,
        "pkg_version": "",
        "files": [{"path": rel, "raw_len": n} for rel, n in files],
    }
    return json.dumps(tree, separators=(",", ":"))


def gather(card_roots: list[pathlib.Path]) -> list[dict]:
    cards: list[dict] = []
    seen: set[str] = set()
    for root in card_roots:
        if not root.is_dir():
            continue
        for toml in sorted(root.rglob("__pmm__.toml")):
            card_dir = toml.parent
            fqn, impl = _read_card_meta(card_dir)
            if not fqn or fqn in seen:
                continue
            src = _muscle_files(card_dir, impl)
            if not src:
                continue  # impl="py" (pysample) or no native muscle: nothing to browse
            seen.add(fqn)
            cards.append({"fqn": fqn, "impl": impl, "dir": card_dir, "files": src,
                          "toml": toml})
    cards.sort(key=lambda c: c["fqn"])
    return cards


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("card_roots", nargs="+", type=pathlib.Path,
                    help="card tree roots (e.g. src/pymergetic/metal ../wasmmod/src/pymergetic)")
    args = ap.parse_args()

    cards = gather(args.card_roots)
    if not cards:
        print("embed_src: no muscle-source cards found", file=sys.stderr)
        return 1

    from io import StringIO

    buf = StringIO()
    buf.write("/* Generated by tools/embed_src.py - edit the card .c/.rs, not this file. */\n\n")
    buf.write('#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n\n')

    for card in cards:
        for path in card["files"]:
            name = _ident(card["fqn"], path.name)
            data = path.read_bytes()
            if b"\x00" in data:
                print(f"embed_src: {path} contains a NUL byte; source embed is text-only", file=sys.stderr)
                return 1
            _emit_array(buf, name, data + b"\x00")  # NUL-terminated for const char * bridge

    buf.write("typedef struct {\n"
              "    const char *rel;\n"
              "    const uint8_t *data;\n"
              "    uint32_t len;\n"
              "} pm_metal_src_file_t;\n\n")
    buf.write("typedef struct {\n"
              "    const char *fqn;\n"
              "    const char *impl;\n"
              "    const pm_metal_src_file_t *files;\n"
              "    uint32_t nfiles;\n"
              "    const char *manifest;\n"
              "    const char *toml;\n"
              "} pm_metal_src_card_t;\n\n")

    # Per-card file tables (after the types above).
    for ci, card in enumerate(cards):
        buf.write(f"static const pm_metal_src_file_t PM_METAL_SRC_FILES_{ci}[] = {{\n")
        for path in card["files"]:
            name = _ident(card["fqn"], path.name)
            buf.write(f'    {{ "{path.name}", s_src_{name}, sizeof(s_src_{name}) - 1u }},\n')
        buf.write("};\n\n")

    for ci, card in enumerate(cards):
        files = [(path.name, path.stat().st_size) for path in card["files"]]
        man = _card_manifest(card["fqn"], files)
        name = _ident(card["fqn"], "manifest")
        toml_name = _ident(card["fqn"], "pmm")
        toml_bytes = card["toml"].read_bytes()
        if b"\x00" in toml_bytes:
            print(f"embed_src: {card['toml']} contains a NUL byte", file=sys.stderr)
            return 1
        _emit_array(buf, name, man.encode("utf-8") + b"\x00")  # NUL-terminated for const char * bridge
        _emit_array(buf, toml_name, toml_bytes + b"\x00")

    # The cards array expands every field in braces (no per-card statics).
    # A TCC guest compiles this header in-kernel (the ksweep / rebuild
    # chain): tcc rejects "arr = { t0, t1 }" (struct-typed constant
    # elements) as non-constant, while the fully braced per-field form is
    # a plain constant expression in both gcc/clang and tcc.
    buf.write("static const pm_metal_src_card_t PM_METAL_SRC_CARDS[] = {\n")
    for ci, card in enumerate(cards):
        name = _ident(card["fqn"], "manifest")
        toml_name = _ident(card["fqn"], "pmm")
        buf.write(f'    {{\n'
            f'        "{card["fqn"]}",\n'
            f'        "{card["impl"]}",\n'
            f'        PM_METAL_SRC_FILES_{ci},\n'
            f'        {len(card["files"])}u,\n'
            f'        (const char *)s_src_{name},\n'
            f'        (const char *)s_src_{toml_name},\n'
            f'    }},\n')
    buf.write("};\n\n")
    buf.write(f"#define PM_METAL_SRC_CARD_COUNT {len(cards)}u\n\n")

    buf.write("/* Table accessors are static: the header is included by every card\n"
              " * that needs the embedded tree (inspect, build), one copy per TU. */\n"
              "static uint32_t pm_metal_src_card_count(void)\n"
              "    __attribute__((unused));\n"
              "static uint32_t pm_metal_src_card_count(void) {\n"
              "    return PM_METAL_SRC_CARD_COUNT;\n}\n\n"
              "static const pm_metal_src_card_t *pm_metal_src_find(const char *fqn)\n"
              "    __attribute__((unused));\n"
              "static const pm_metal_src_card_t *pm_metal_src_find(const char *fqn) {\n"
              "    if (fqn == NULL) return NULL;\n"
              "    size_t lo = 0, hi = PM_METAL_SRC_CARD_COUNT;\n"
              "    while (lo < hi) {\n"
              "        size_t mid = lo + (hi - lo) / 2u;\n"
              "        int c = strcmp(PM_METAL_SRC_CARDS[mid].fqn, fqn);\n"
              "        if (c == 0) return &PM_METAL_SRC_CARDS[mid];\n"
              "        if (c < 0) lo = mid + 1u; else hi = mid;\n"
              "    }\n"
              "    return NULL;\n"
              "}\n\n")

    text = buf.getvalue()
    out = pathlib.Path(args.output)
    if out.is_file() and out.read_text(encoding="utf-8") == text:
        return 0
    out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
