"""Route adapter: Inspect stubs → FastAPI (CDN / ASGI hosts).

FastAPIShell mounts the same `(json, status)` handlers from `api.py` that
MicrodotShell uses — swap shells, handlers stay identical. The CDN has no
loaded container registry, so the registry handlers report /not-live; the
contract (paths + capabilities + self-desc) is declared for symmetry.
"""

import json

from . import api as _api
from .stubs import ENDPOINT_STUBS, capabilities as make_capabilities, self_description

try:
    from fastapi import FastAPI
    from fastapi.responses import JSONResponse
except ImportError:  # pragma: no cover — guest / no FastAPI
    FastAPI = None
    JSONResponse = None


def _json(raw):
    if isinstance(raw, str):
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            return {"raw": raw}
    return raw


def _json_response(*args, **kwargs):
    """Build a FastAPI JSONResponse once FastAPI is known present.

    ``JSONResponse`` is a module global shadowed to ``None`` when FastAPI is
    unavailable (guest / no-FastAPI seats). ``FastAPIShell`` cannot even
    construct in that case — ``__init__`` raises when ``FastAPI is None`` — so
    this helper is only ever called here when FastAPI is present. The assert is
    the single narrowing point; handlers call this instead of ``JSONResponse``
    directly, so pyright no longer sees a possibly-``None`` callable.
    """
    assert JSONResponse is not None  # FastAPIShell() guards FastAPI present
    return JSONResponse(*args, **kwargs)


class FastAPIShell:
    def __init__(self, role="cdn", theme="cdn", app=None, *, include_health=True):
        if FastAPI is None:
            raise RuntimeError("fastapi not available")
        self.role = role
        self.theme = theme
        self.include_health = include_health
        self.app = app if app is not None else FastAPI()
        self._register()

    def capabilities(self):
        # A seat serving static from disk (CDN mounts StaticFiles from the app's
        # directory) must not claim embedded bytes: Metal embeds, CDN doesn't.
        if self.role == "cdn":
            return make_capabilities(
                self.role, self.theme, fastapi=True,
                static_embed=False, static_backend="none")
        return make_capabilities(self.role, self.theme, fastapi=True)

    def self_desc(self):
        return self_description(self.role, self.theme)

    def add_routes(self, app=None, *, include_health=None):
        """Mount routes onto an existing FastAPI app (or self.app)."""
        if app is not None:
            self.app = app
        if include_health is not None:
            self.include_health = include_health
        self._register()
        return self.app

    def _register(self):
        app = self.app
        shell = self

        if self.include_health:

            @app.get("/health")
            async def health():
                return _json(_api.health()[0])

        @app.get("/capabilities")
        async def capabilities():
            return _json(_api.capabilities(shell.role, shell.theme, shell.capabilities())[0])

        @app.get("/inspect/self")
        async def inspect_self():
            return _json(_api.self_desc(shell.self_desc())[0])

        @app.get("/inspect/reg")
        async def inspect_reg():
            return _json(_api.reg_all()[0])

        @app.get("/inspect/reg/completeness")
        async def inspect_reg_completeness(fmt: str = "json"):
            # No live registry on the CDN → not-live, but the path must answer
            # so the shared SPA doesn't 404 on the completeness panel.
            tree = {
                "schema": 1,
                "method_count": 0,
                "gap_count": 0,
                "modules": [],
                "gaps": [],
                "note": "not_live",
            }
            return _json(json.dumps(tree))

        @app.get("/inspect/reg/{module}")
        async def inspect_reg_module(module: str):
            body, st = _api.reg_module(module)
            return _json_response(_json(body), status_code=st)

        @app.get("/inspect/reg/{module}/{func}")
        async def inspect_reg_module_func(module: str, func: str):
            body, st = _api.reg_module_func(module, func)
            return _json_response(_json(body), status_code=st)

        @app.get("/inspect/call/{module}/{func}")
        async def inspect_call(module: str, func: str, a0: str = "", a1: str = ""):
            body, st = _api.call(module, func, {"a0": a0, "a1": a1})
            return _json_response(_json(body), status_code=st)

        for method, path, implemented in ENDPOINT_STUBS:
            if (
                implemented
                or path.startswith("/inspect/reg/")
                or path.startswith("/inspect/call/")
                or path.count("<")  # path templates handled above
            ):
                continue

            async def not_impl(p=path):
                return _json_response(
                    {"error": "NotImplemented", "path": p},
                    status_code=501,
                )

            app.add_api_route(path, not_impl, methods=[method])
