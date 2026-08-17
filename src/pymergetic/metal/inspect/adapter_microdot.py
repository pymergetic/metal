"""Route adapter: Inspect stubs → Microdot, mounted on ASGI (not a second listen)."""

import json

from .face import handle as c_handle
from .stubs import ENDPOINT_STUBS, capabilities as make_capabilities, self_description

try:
    from pymergetic.metal.net.microdot import Microdot
except ImportError:  # pragma: no cover — host tooling
    Microdot = None


def _c_json(method, path):
    st, body = c_handle(method, path)
    if not body:
        return {"error": "empty", "status": st}, st
    try:
        return json.loads(body), st
    except Exception:
        return body, st


class MicrodotAdapter:
    def __init__(self, role="metal", theme="metal"):
        if Microdot is None:
            raise RuntimeError("microdot not available")
        self.role = role
        self.theme = theme
        self.app = Microdot()
        self._register()

    def capabilities(self):
        if self.role == "cdn":
            return make_capabilities(self.role, self.theme, fastapi=False)
        body, st = _c_json("GET", "/capabilities")
        if st == 200 and isinstance(body, dict):
            return body
        return make_capabilities(self.role, self.theme, fastapi=False)

    def self_desc(self):
        if self.role == "cdn":
            return self_description(self.role, self.theme)
        body, st = _c_json("GET", "/inspect/self")
        if st == 200:
            return body
        return self_description(self.role, self.theme)

    def attach_asgi(self):
        """JSON paths are already route_fn'd by inspect C init. Same listen."""
        return self.app.attach_asgi()

    def _register(self):
        app = self.app
        adapter = self

        @app.get("/health")
        async def health(request):
            body, st = _c_json("GET", "/health")
            return body, st

        @app.get("/capabilities")
        async def capabilities(request):
            return adapter.capabilities()

        @app.get("/inspect/self")
        async def inspect_self(request):
            return adapter.self_desc()

        @app.get("/inspect/reg")
        async def inspect_reg(request):
            body, st = _c_json("GET", "/inspect/reg")
            return body, st

        @app.get("/inspect/reg/seats")
        async def inspect_reg_seats(request):
            body, st = _c_json("GET", "/inspect/reg/seats")
            return body, st

        @app.get("/inspect/reg/completeness")
        async def inspect_reg_completeness(request):
            args = getattr(request, "args", None) or {}
            q = "/inspect/reg/completeness"
            if hasattr(args, "get") and str(args.get("fmt", "")) == "tree":
                q = "/inspect/reg/completeness?fmt=tree"
            body, st = _c_json("GET", q)
            return body, st

        @app.get("/inspect/reg/method")
        async def inspect_reg_method(request):
            args = getattr(request, "args", None) or {}
            module = args.get("module", "") if hasattr(args, "get") else ""
            func = args.get("func", "") if hasattr(args, "get") else ""
            if not module or not func:
                return {"error": "need module and func query"}, 400
            body, st = _c_json("GET", "/inspect/reg/" + str(module) + "/" + str(func))
            return body, st

        @app.get("/inspect/reg/<module>")
        async def inspect_reg_module(request, module):
            body, st = _c_json("GET", "/inspect/reg/" + str(module))
            return body, st

        @app.get("/inspect/reg/<module>/<method>")
        async def inspect_reg_method_path(request, module, method):
            body, st = _c_json("GET", "/inspect/reg/" + str(module) + "/" + str(method))
            return body, st

        for method, path, implemented in ENDPOINT_STUBS:
            if implemented:
                continue

            @app.route(path, methods=[method])
            async def not_impl(request, p=path):
                return {"error": "NotImplemented", "path": p}, 501
