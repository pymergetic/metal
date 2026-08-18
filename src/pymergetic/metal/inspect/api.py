"""Transport-agnostic Inspect API core: pure `(json, status)` handlers.

One handler core, two thin shells. Every endpoint here returns a JSON string
and an HTTP status, with zero dependency on the C server or any web framework.
FastAPIShell and MicrodotShell both mount these — swap the shell, the handlers
stay identical.

The registry face (_modules / _export_count) resolves the live registry when
one exists (Metal seat: pymergetic.wasmmod), and degrades to the /not-live
contract elsewhere (CDN: no loaded container registry). This is the same data
the C server produces on-device, so the CDN frontend and the Metal frontend
agree endpoint-for-endpoint.
"""

try:
    import json
except ImportError:  # pragma: no cover — µPy
    import ujson as json


def _registry():
    """Best available registry face. Returns a (modules, export_count) pair."""
    try:
        import pymergetic.wasmmod as w
    except Exception:
        return lambda: [], lambda fqn: 0
    mods = getattr(w, "modules", None)
    if mods is None:
        return lambda: [], lambda fqn: 0
    exp = getattr(w, "export_count", None) or (lambda fqn: 0)
    return lambda: list(mods()), lambda fqn: int(exp(fqn))


def health():
    return json.dumps({"ok": True}), 200


def capabilities(role, theme, caps):
    return json.dumps(caps), 200


def self_desc(body):
    return json.dumps(body), 200


def reg_all():
    """/inspect/reg — full module ledger from the live registry (or not-live)."""
    reg = _registry()
    mods = reg[0]()
    if not _live():
        out = {
            "schema": 1,
            "method_count": 0,
            "gap_count": 0,
            "modules": [],
            "methods": [],
            "gaps": [],
            "note": "not_live",
        }
        return json.dumps(out), 200
    methods = [
        {"module": fqn, "func": "*", "exports": reg[1](fqn)}
        for fqn in sorted(mods)
    ]
    return (
        json.dumps(
            {
                "schema": 1,
                "method_count": len(methods),
                "gap_count": 0,
                "modules": sorted(mods),
                "methods": methods,
                "gaps": [],
                "note": "live_registry",
            },
            separators=(",", ":"),
        ),
        200,
    )


def _live():
    """True when a live registry is reachable (Metal)."""
    try:
        return callable(_registry()[0]) and bool(_registry()[0]())
    except Exception:
        return False


def reg_module(module):
    """/inspect/reg/<module> — that module's export count (present/alive)."""
    reg = _registry()
    mods = reg[0]()
    if module in mods:
        body = {"module": module, "present": True, "exports": reg[1](module)}
    else:
        body = {"module": module, "present": False, "live": bool(mods)}
    return json.dumps(body, separators=(",", ":")), 200


def reg_module_func(module, func):
    """/inspect/reg/<module>/<func> — one export's presence."""
    mods = _registry()[0]()
    if module in mods:
        body = {"module": module, "func": func, "present": True}
    else:
        body = {"module": module, "func": func, "present": False, "live": bool(mods)}
    return json.dumps(body, separators=(",", ":")), 200


def call(module, func, args=None):
    """/inspect/call/<module>/<func> — invoke a container export.

    The API core itself cannot execute a raw C/Rust value-ABI export; that is
    the C server's job (and only for container wasm/aot/elf modules, never
    resident native cards). On any seat without a live container registry this
    returns the same truthful /native_module refusal the C server emits.
    """
    detail = "rpc only for a loaded container (wasm/aot/elf) device"
    return json.dumps(
        {"module": module, "func": func, "status": 1, "error": "native_module",
         "detail": detail},
        separators=(",", ":"),
    ), 501
