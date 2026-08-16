"""Route adapter: Inspect stubs → Microdot (framework-independent registry)."""

import json

from .stubs import ENDPOINT_STUBS, capabilities as make_capabilities, self_description

try:
    from pymergetic.metal.net.microdot import Microdot
except ImportError:  # pragma: no cover — host tooling
    Microdot = None


def _ledger_snapshot():
    try:
        from pymergetic.metal.reg import ledger_json

        return json.loads(ledger_json())
    except Exception:
        return {
            "schema": 1,
            "method_count": 0,
            "gap_count": 0,
            "methods": [],
            "note": "reg_ledger_unavailable",
        }


def _ledger_module(module):
    try:
        from pymergetic.metal.reg import ledger_module_json

        return json.loads(ledger_module_json(module))
    except Exception:
        return {"error": "not_found", "module": module}


def _ledger_method(module, func):
    try:
        from pymergetic.metal.reg import ledger_method_json

        return json.loads(ledger_method_json(module, func))
    except Exception:
        return {"error": "not_found", "module": module, "func": func}


def _qbool(args, key):
    if not hasattr(args, "get"):
        return False
    v = args.get(key, "")
    if v is True or v is False:
        return bool(v)
    s = str(v).lower()
    return s in ("1", "true", "yes", "on")


def _completeness(request, fmt_default="json"):
    try:
        from pymergetic.metal.reg import completeness
    except Exception:
        return {"error": "reg_completeness_unavailable"}, 501
    args = getattr(request, "args", None) or {}
    module = args.get("module", "") if hasattr(args, "get") else ""
    gaps_only = _qbool(args, "gaps_only")
    detail = _qbool(args, "detail")
    fmt = args.get("fmt", fmt_default) if hasattr(args, "get") else fmt_default
    if not fmt:
        fmt = fmt_default
    kwargs = {"gaps_only": gaps_only, "detail": detail, "fmt": str(fmt)}
    if module:
        kwargs["module"] = str(module)
    text = completeness(**kwargs)
    if str(fmt) == "tree":
        return text, 200
    try:
        return json.loads(text)
    except Exception:
        return {"error": "bad_completeness_json", "raw": text}, 500


class MicrodotAdapter:
    def __init__(self, role="metal", theme="metal"):
        if Microdot is None:
            raise RuntimeError("microdot not available")
        self.role = role
        self.theme = theme
        self.app = Microdot()
        self._register()

    def capabilities(self):
        return make_capabilities(self.role, self.theme, fastapi=False)

    def self_desc(self):
        return self_description(self.role, self.theme)

    def _register(self):
        app = self.app
        adapter = self

        @app.get("/health")
        async def health(request):
            return {"ok": True}

        @app.get("/capabilities")
        async def capabilities(request):
            return adapter.capabilities()

        @app.get("/inspect/self")
        async def inspect_self(request):
            return adapter.self_desc()

        @app.get("/inspect/reg")
        async def inspect_reg(request):
            snap = _ledger_snapshot()
            if isinstance(snap, dict):
                snap = dict(snap)
                snap["completeness_url"] = "/inspect/reg/completeness"
                snap["completeness_tree_url"] = "/inspect/reg/completeness?fmt=tree"
            return snap

        @app.get("/inspect/reg/seats")
        async def inspect_reg_seats(request):
            try:
                from pymergetic.metal.reg import seats_json

                return json.loads(seats_json())
            except Exception:
                return {"error": "reg_seats_unavailable"}

        # Static segments before <module> catch-all.
        @app.get("/inspect/reg/completeness")
        async def inspect_reg_completeness(request):
            return _completeness(request, fmt_default="json")

        @app.get("/inspect/reg/method")
        async def inspect_reg_method(request):
            args = getattr(request, "args", None) or {}
            module = args.get("module", "") if hasattr(args, "get") else ""
            func = args.get("func", "") if hasattr(args, "get") else ""
            if not module or not func:
                return {"error": "need module and func query"}, 400
            return _ledger_method(module, func)

        @app.get("/inspect/reg/<module>")
        async def inspect_reg_module(request, module):
            return _ledger_module(module)

        @app.get("/inspect/reg/<module>/<method>")
        async def inspect_reg_method_path(request, module, method):
            return _ledger_method(module, method)

        for method, path, implemented in ENDPOINT_STUBS:
            if implemented:
                continue

            @app.route(path, methods=[method])
            async def not_impl(request, p=path):
                # Tuple form avoids Response/NoCaseDict (dict subclass attrs
                # break under MICROPY_CONFIG_ROM_LEVEL_MINIMUM).
                return {"error": "NotImplemented", "path": p}, 501
