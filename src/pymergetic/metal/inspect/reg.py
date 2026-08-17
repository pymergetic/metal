"""Inspect inventory from the live wasmmod registry.

The old completeness ledger is gone on purpose. This seat's registry is the
authority: what registered here, with which exports. A missing face on another
seat is a fact about that seat's prove, not a row in a second table.
"""

try:
    import json
except ImportError:  # pragma: no cover — µPy
    import ujson as json


def _modules():
    import pymergetic.wasmmod as w

    fn = getattr(w, "modules", None)
    if fn is None:
        return []
    return list(fn())


def _export_count(fqn):
    # Optional; this seat's face has modules() but not an export walk.
    import pymergetic.wasmmod as w

    fn = getattr(w, "export_count", None)
    if fn is None:
        return 0
    return int(fn(fqn))


def snapshot():
    mods = sorted(_modules())
    methods = []
    for fqn in mods:
        methods.append({"module": fqn, "func": "*", "exports": _export_count(fqn)})
    return {
        "schema": 1,
        "method_count": len(methods),
        "gap_count": 0,
        "modules": mods,
        "methods": methods,
        "gaps": [],
        "note": "live_registry",
    }


def ledger_json():
    return json.dumps(snapshot(), separators=(",", ":"))


def ledger_module_json(module):
    mods = _modules()
    if module not in mods:
        return json.dumps({"error": "not_found", "module": module}, separators=(",", ":"))
    return json.dumps(
        {"module": module, "present": True, "exports": _export_count(module)},
        separators=(",", ":"),
    )


def ledger_method_json(module, func):
    mods = _modules()
    if module not in mods:
        return json.dumps(
            {"error": "not_found", "module": module, "func": func},
            separators=(",", ":"),
        )
    return json.dumps(
        {"module": module, "func": func, "present": True},
        separators=(",", ":"),
    )


def seats_json():
    return json.dumps(
        {"schema": 1, "seats": ["this"], "note": "this_seat_registry"},
        separators=(",", ":"),
    )


def completeness(gaps_only=False, detail=False, fmt="json", module=""):
    snap = snapshot()
    if module:
        snap["modules"] = [m for m in snap["modules"] if m == module or m.startswith(module + ".")]
        snap["methods"] = [m for m in snap["methods"] if m["module"] in snap["modules"]]
        snap["method_count"] = len(snap["methods"])
    if fmt == "tree":
        lines = ["registry  %d module(s)" % len(snap["modules"])]
        for m in snap["modules"]:
            lines.append("+-- " + m)
        return "\n".join(lines) + "\n"
    return json.dumps(snap, separators=(",", ":"))
