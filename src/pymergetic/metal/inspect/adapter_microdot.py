"""Route adapter: Inspect stubs → Microdot, mounted on ASGI (not a second listen).

MicrodotShell: the same `(json, status)` handlers from `api.py` that
FastAPIShell mounts, expressed as Microdot routes. On the Metal seat the C
inspect handler already serves these exact paths via route_fn on the asgi
server, so `attach_asgi()` does not open a second socket nor re-register the
paths — it returns the shared asgi face. Swap FastAPIShell/MicrodotShell and
the handlers are identical.
"""

import json

from typing import Any, cast

from . import api as _api
from .stubs import ENDPOINT_STUBS, capabilities as make_capabilities, self_description

try:
    from pymergetic.metal.net.microdot import Microdot
except ImportError:  # pragma: no cover — host tooling
    Microdot = None

try:
    from .face import handle as _c_handle
    _HAS_C = True
except Exception:  # pragma: no cover — no Metal card (CDN host)
    _c_handle = None
    _HAS_C = False


def _c_json(method, path):
    if not _HAS_C or _c_handle is None:
        return None, 404
    st, body = _c_handle(method, path)
    if not body:
        return {"error": "empty", "status": st}, st
    try:
        return json.loads(body), st
    except Exception:
        return body, st


class MicrodotShell:
    def __init__(self, role="metal", theme="metal", caps=None):
        if Microdot is None:
            raise RuntimeError("microdot not available")
        self.role = role
        self.theme = theme
        self.caps = caps
        self.app = Microdot()
        self._register()

    def capabilities(self):
        if self.caps is not None:
            return self.caps
        return make_capabilities(self.role, self.theme, fastapi=False)

    def self_desc(self):
        return self_description(self.role, self.theme)

    def attach_asgi(self):
        """JSON paths are already route_fn'd by inspect C init. Same listen."""
        # `attach_asgi` is a project extension monkey-patched onto the vendored
        # Microdot class in `net.microdot.__init__` (`Microdot.attach_asgi =
        # _asgi`), so the type checker has no static declaration for it.
        return cast(Any, self.app).attach_asgi()

    def _register(self):
        app = self.app
        shell = self

        @app.get("/health")
        async def health(request):
            return _ok(*_api.health())

        @app.get("/capabilities")
        async def capabilities(request):
            return _ok(*_api.capabilities(shell.role, shell.theme, shell.capabilities()))

        @app.get("/inspect/self")
        async def inspect_self(request):
            return _ok(*_api.self_desc(shell.self_desc()))

        @app.get("/inspect/reg")
        async def inspect_reg(request):
            return _ok(*_api.reg_all())

        @app.get("/inspect/reg/<module>")
        async def inspect_reg_module(request, module):
            return _ok(*_api.reg_module(module))

        @app.get("/inspect/reg/<module>/<method>")
        async def inspect_reg_method_path(request, module, method):
            return _ok(*_api.reg_module_func(module, method))

        @app.get("/inspect/call/<module>/<method>")
        async def inspect_call(request, module, method):
            args = getattr(request, "args", None) or {}
            q = {}
            if hasattr(args, "items"):
                q = {
                    k: v
                    for k, v in args.items()
                    if str(k).startswith("a")
                }
            return _ok(*_api.call(module, method, q))

        @app.get("/inspect/reg/method")
        async def inspect_reg_method(request):
            args = getattr(request, "args", None) or {}
            module = args.get("module", "") if hasattr(args, "get") else ""
            func = args.get("func", "") if hasattr(args, "get") else ""
            if not module or not func:
                return _ok(json.dumps({"error": "need module and func query"}), 400)
            body, st = _c_json("GET", "/inspect/reg/" + str(module) + "/" + str(func))
            return _ok(body, st)

        @app.get("/inspect/reg/completeness")
        async def inspect_reg_completeness(request):
            # The completeness tree is a C-server walk (multi-callee / face /
            # honesty gaps) — the same shape the SPA expects. Delegate to the
            # live C handler; unavailable on a pure-Python seat → not found.
            args = getattr(request, "args", None) or {}
            q = "/inspect/reg/completeness"
            if hasattr(args, "get") and str(args.get("fmt", "")) == "tree":
                q = "/inspect/reg/completeness?fmt=tree"
            body, st = _c_json("GET", q)
            return _ok(body, st)

        # /seats mirrors the C server; only reachable via the C face.
        @app.get("/inspect/reg/seats")
        async def inspect_reg_seats(request):
            body, st = _c_json("GET", "/inspect/reg/seats")
            return _ok(body, st)

        for method, path, implemented in ENDPOINT_STUBS:
            if implemented:
                continue

            @app.route(path, methods=[method])
            async def not_impl(request, p=path):
                return _ok(json.dumps({"error": "NotImplemented", "path": p}), 501)


def _ok(body, status):
    """Microdot accepts (json_string, status) or (dict, status)."""
    try:
        return json.loads(body), status
    except Exception:
        return body, status
