#!/usr/bin/env python3
"""Generate missing `<name>.rs` umbrella/barrel files in the metal card tree.

For every card/namespace that carries a Rust face (`__exports__.rs` /
`__impl__.rs`) or routes to Rust-reachable children but has no `<name>.rs`
barrel at `path == module`, emit the thin routing file so the two faces stay
in lock-step:

- leaf C card  -> face-reexport (`#[path = "<name>/__exports__.rs"] mod
  export; pub use export::*;`), same posture as `util/mem.rs` / `wasmmod/io.rs`.
- namespace    -> child-module declarations (`#[path] pub mod <child>;`),
  plus a face-reexport when the namespace carries its own `__exports__.rs`.

This is a barrel, never a second implementaion: the real C muscle stays in
`__impl__.c` and this file only makes `pymergetic::metal::<fqn>` resolve.
Run from the repo root of `extmod/metal` (the `src/` tree):

    python3 tools/gen_rs_barrels.py

Idempotent: it only writes a file when `<name>.rs` is absent, and only
patches a parent barrel when a child `pub mod` is missing.
"""

from __future__ import annotations

import os
import sys
import tomllib

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src", "pymergetic", "metal")
SKIP_PARTS = {"__pycache__", "www", "static", "__init__"}


def impl_of(carddir: str):
    p = os.path.join(carddir, "__pmm__.toml")
    if not os.path.isfile(p):
        return None
    try:
        with open(p, "rb") as f:
            return tomllib.load(f).get("impl")
    except Exception:
        return None


def walk_nodes():
    nodes = {}
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_PARTS and not d.startswith("__")]
        rel = os.path.relpath(dirpath, ROOT)
        parts = rel.split(os.sep)
        if any(seg in ("www", "static") for seg in parts):
            continue
        name = parts[-1] if rel != "." else "metal"
        has_impl = any(f.startswith("__impl__") for f in filenames)
        exrs = os.path.exists(os.path.join(dirpath, "__exports__.rs"))
        imprs = os.path.exists(os.path.join(dirpath, "__impl__.rs"))
        h = os.path.exists(os.path.join(os.path.dirname(dirpath), name + ".h"))
        r = os.path.exists(os.path.join(os.path.dirname(dirpath), name + ".rs"))
        nodes[rel] = {
            "name": name,
            "rel": rel,
            "abs": dirpath,
            "impl": impl_of(dirpath),
            "leaf": has_impl,
            "ns": bool(dirnames),
            "exrs": exrs,
            "imprs": imprs,
            "has_h": h,
            "has_rs": r,
            "children": sorted(dirnames),
        }
    return nodes


def fqn(rel: str) -> str:
    if rel == ".":
        return "pymergetic.metal"
    return "pymergetic.metal." + rel.replace(os.sep, ".")


def leaf_barrel(node) -> str:
    name = node["name"]
    if node["imprs"]:
        src = name + "/__impl__.rs"
        return (
            "//! %s — barrel: not optional, this is what makes\n"
            "//! `pymergetic::metal::%s` resolve at all (Rust's own `use`/`mod` needs\n"
            "//! a real item at this path, matching path == module). Reexports the\n"
            "//! real impl in %s under the module's real name.\n"
            "#[path = \"%s\"]\n"
            "mod r#impl;\n"
            "pub use r#impl::*;\n"
        ) % (
            fqn(node["rel"]),
            node["rel"].replace(os.sep, "::"),
            src,
            src,
        )
    # C leaf. The generated `__exports__.rs` mirror is a C-boundary by-product
    # of `pymergetic.util.gen`, not a crate face (it passes raw C typedefs like
    # `uint16_t` / `volatile` through and does not compile in this no_std
    # crate). So the barrel is a hollow declaration — same posture as
    # `metal/dt.rs` / `metal.drivers.net` — that makes the Rust path resolve and
    # nothing else. The real logic is `<name>/__impl__.c`.
    logic = name + "/__impl__.c"
    return (
        "//! %s — barrel: not optional, this is what makes\n"
        "//! `pymergetic::metal::%s` resolve at all (Rust's own `use`/`mod` needs\n"
        "//! a real item at this path, matching path == module). Hollow RS path:\n"
        "//! the muscle is C (`%s`); the generated `__exports__.rs` mirror is not\n"
        "//! a crate face and stays unlinked here.\n"
    ) % (
        fqn(node["rel"]),
        node["rel"].replace(os.sep, "::"),
        logic,
    )


def ns_barrel(node) -> str:
    if node["rel"] == ".":
        self_path = "metal"
    else:
        self_path = node["rel"].replace(os.sep, "::")
    lines = [
        "//! %s — barrel: not optional, this is what makes" % fqn(node["rel"]),
        "//! `pymergetic::metal::%s` resolve at all (Rust's own `use`/`mod` needs" % self_path,
        "//! a real item at this path, matching path == module).",
    ]
    if node["exrs"] and node["imprs"]:
        lines += ["//! Reexports this namespace's own impl and declares its children."]
    elif node["exrs"]:
        lines += ["//! C namespace: declares its Rust-reachable children (below)."]
    lines.append("")
    for child in node["children"]:
        lines.append("#[path = \"%s.rs\"]" % os.path.join(node["name"], child))
        lines.append("pub mod %s;" % child)
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    nodes = walk_nodes()
    created = []
    for rel, node in sorted(nodes.items()):
        if node["has_rs"]:
            continue
        # Only wire nodes that either carry a Rust face or have children that do.
        has_rusted_desc = (
            node["exrs"]
            or node["imprs"]
            or any(any(f.startswith("__export") or f.startswith("__impl__.rs") for f in os.listdir(c))
                   for c, sub in _subdirs(node))
        )
        if not has_rusted_desc:
            continue
        if node["leaf"] and not node["ns"]:
            content = leaf_barrel(node)
        else:
            content = ns_barrel(node)
        target = os.path.join(os.path.dirname(node["abs"]), node["name"] + ".rs")
        with open(target, "w") as f:
            f.write(content)
        created.append(os.path.relpath(target, ROOT))
    print("%d barrel(s) created:" % len(created))
    for c in created:
        print("  + %s" % c)
    return 0


def _subdirs(node):
    out = []
    for c in node["children"]:
        p = os.path.join(node["abs"], c)
        out.append((p, os.listdir(p) if os.path.isdir(p) else []))
    return out


if __name__ == "__main__":
    sys.exit(main())
