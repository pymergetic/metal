"""CDN-shaped introspect API for the on-device seat, served by the deferred pump.

The Metal seat is a CDN of its own cards: the shared Inspect commander (the same
ES modules the CDN serves, vendored under www/static/inspect) reaches the seat's
package pages through the exact `/artifacts/lead/<file>/...` and `/packages` /
`/packages/<name>/versions` contract it reaches a real CDN. This module is that
seat-side implementation, backed by the live wasmmod registry instead of a SQL
database.

A seat card artifact (`<fqn>.card`, encoding "registry") has no downloadable
binary: its cargo is the set of registered C/Rust exports. So the commander's
binary-shaped endpoints (sections/raw, disasm, files) return the same *truthful*
"No code so far" minimal answers the FastAPI adapter's non-live stubs return for
a card, while `/inspect`, `/sections`, `/symbols`, `/packages` and the navigation
endpoints are real data from the registry (module FQNs, export counts and — via
the live C face — every export's real name/kind/signature). This keeps the SPA
never breaking (every route answers, never 404s) and never lying about bytes the
card does not have.

Every handler returns a `(json_string, status)` pair just like `api.py`, so the
same core could be mounted on FastAPI or Microdot; the seat serves it from
`metal_packs`'s deferred pump alongside `/packs/*`.
"""

try:
    import json
except ImportError:  # pragma: no cover - MicroPython
    import ujson as json

_ART_PREFIX = "/artifacts/lead/"


def _json(obj):
    return json.dumps(obj, separators=(",", ":"))


def _role_of(fqn):
    if fqn.startswith("pymergetic.metal.arch.") or fqn.endswith(".arch"):
        return "arch"
    if fqn.startswith("pymergetic.metal.unix."):
        return "host"
    if fqn.startswith("pymergetic.metal."):
        return "kernel"
    return ""


def _modules():
    """The seat's full module ledger from the live wasmmod registry."""
    try:
        import pymergetic.wasmmod as w
    except Exception:
        return []
    mods = getattr(w, "modules", None)
    if mods is None:
        return []
    try:
        return sorted(list(mods()))
    except Exception:
        return []


def _exports(fqn):
    """Per-export docs (name/kind/sig) from the live C registry face.

    The Python wasmmod face exposes module FQNs and per-module export counts but
    not per-export details; the C inspect card does. `pymergetic.metal.inspect`
    exposes the same handle/body the consoled /inspect/reg/<fqn> route serves, so
    we ask it for the real exports. Returns None when the C face is absent (a
    pure-Python seat) or the module is unknown.
    """
    try:
        import pymergetic.metal.inspect as mi
    except Exception:
        return None
    try:
        st = mi.handle("GET", "/inspect/reg/" + fqn)
    except Exception:
        return None
    if st is None:
        return None
    try:
        doc = json.loads(mi.body())
    except Exception:
        return None
    if doc is None or not isinstance(doc, dict) or doc.get("module") != fqn:
        return None
    return doc.get("exports")


def _symbols(fqn, docs):
    """The card's registered exports as commander symbols (kind "export").
    """
    out = []
    if docs is None:
        return out
    for i, d in enumerate(docs):
        name = isinstance(d, dict) and d.get("name") or ""
        kind = isinstance(d, dict) and d.get("kind") or "fn"
        if not name:
            continue
        out.append({
            "name": name,
            "kind": "export",
            "binding": "global",
            "size": 0,
            "offset": i,
            "section_index": 0,
            "tag": "reserved" if kind == "reserved" else kind,
        })
    return out


def _sections(fqn, exports):
    export_count = len(exports or [])
    return [{
        "index": 0,
        "name": "exports",
        "role": "symbols",
        "type_id": 1,
        "size": export_count,
        "offset": 0,
    }]


def _inspect(fqn, exports, symbols):
    sections = _sections(fqn, exports)
    exports_json = []
    for d in exports or []:
        item = {}
        if isinstance(d, dict):
            name = d.get("name")
            if name:
                item["name"] = name
            sig = d.get("sig")
            if sig:
                item["sig"] = sig
            kind = d.get("kind")
            if kind:
                item["kind"] = kind
        if item:
            exports_json.append(item)
    count = len(exports or [])
    size = sum(len(e.get("sig", "")) for e in exports_json) if exports_json else 0
    return {
        "kind": "card",
        "encoding": "registry",
        "signed": False,
        "naked_size": count,
        "size": size,
        "has_dwarf": False,
        "pack": None,
        "source": None,
        "sections": sections,
        "symbols": _symbols_only(symbols),
        "exports": exports_json,
        "error": None,
    }


def _symbols_only(symbols):
    out = []
    for s in symbols:
        row = {
            "name": s["name"],
            "kind": s["kind"],
            "binding": s["binding"],
            "size": s["size"],
            "offset": s["offset"],
            "section_index": s["section_index"],
        }
        if s.get("tag"):
            row["tag"] = s["tag"]
        out.append(row)
    return out


def _not_found(fqn):
    return _json({"detail": "no such card artifact", "artifact": fqn}).encode(), 404, False


def _art_route(path):
    """Split `/artifacts/lead/<fqn>.card/<op>` -> (fqn, op) or (None, None)."""
    if not path.startswith(_ART_PREFIX):
        return None, None
    rest = path[len(_ART_PREFIX):]
    if not rest:
        return None, None
    slash = rest.find("/")
    if slash < 0:
        fname = rest
        op = ""
    else:
        fname = rest[:slash]
        op = rest[slash + 1:]
    fqn = fname[:-len(".card")] if fname.endswith(".card") else ""
    if not fqn:
        return None, None
    return fqn, op


def route(path):
    """Handle a CDN introspect request. Returns (body_bytes, status, binary).
    """
    name = path.split("?")[0]

    # Package navigation (commander's `packages` root — cdnPrefix() is "").
    if name == "/packages" or name == "/packages/":
        rows = [{"name": fqn, "role": _role_of(fqn)} for fqn in _modules()]
        return _json(rows).encode(), 200, False
    if name.startswith("/packages/"):
        pkg = name[len("/packages/"):]
        pkg = pkg.split("/")[0]
        ver = name[len("/packages/") + len(pkg):].strip("/")
        if ver == "versions":
            mods = _modules()
            if pkg in mods:
                one = [{"channel": "lead", "version": "", "label": "lead",
                        "artifact_count": len(_exports(pkg) or [])}]
            else:
                one = []
            return _json(one).encode(), 200, False
        # /packages/<name>  -> one version's artifact list for navigation
        mods = _modules()
        if pkg not in mods:
            return _json({"detail": "no such package", "package": pkg}).encode(), 404, False
        return _json({
            "name": pkg,
            "channel": "lead",
            "role": _role_of(pkg),
            "artifacts": [{"path": pkg + ".card"}],
        }).encode(), 200, False

    if not path.startswith(_ART_PREFIX):
        return _json({"detail": "not found"}).encode(), 404, False

    fqn, op = _art_route(name)
    if fqn is None:
        return _json({"detail": "malformed artifact path", "path": name}).encode(), 404, False
    mods = _modules()
    if fqn not in mods:
        return _not_found(fqn)

    # A bare artifact root (no trailing op) is the raw "binary" fetch the hex
    # pane does. A registry card has no bytes; return an empty body truthfully.
    if not op:
        return b"", 200, True

    docs = _exports(fqn)
    symbols = _symbols(fqn, docs)

    if op == "inspect":
        return _json(_inspect(fqn, docs, symbols)).encode(), 200, False
    if op == "sections":
        return _json(_sections(fqn, docs)).encode(), 200, False
    if op == "symbols":
        return _json(_symbols_only(symbols)).encode(), 200, False
    if op.startswith("sections/raw"):
        # A card has no section bytes to dump: truthful empty hex, never 404.
        return b"", 200, True
    if op.startswith("disasm"):
        # No executable bits in a registry card: empty list (commander's
        # formatAsm shows "empty").
        return _json([]).encode(), 200, False
    if op.startswith("files/mpy-disasm"):
        return _json([]).encode(), 200, False
    if op.startswith("files"):
        sub = op[len("files"):]
        if sub.startswith("/raw"):
            return b"", 200, True
        # `/files` and `/files?path=` — a card carries no embedded source tree.
        return _json({}).encode(), 200, False
    if op.startswith("locations"):
        # No DWARF/address → no source locations for a registry card.
        return _json([]).encode(), 200, False
    if op.startswith("addr2line"):
        return _json([]).encode(), 200, False
    return _json({"detail": "unknown artifact op", "op": op}).encode(), 404, False
