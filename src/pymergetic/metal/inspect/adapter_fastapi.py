"""Route adapter: Inspect stubs → FastAPI (CDN / ASGI hosts)."""

from .stubs import ENDPOINT_STUBS, capabilities as make_capabilities, self_description

try:
    from fastapi import FastAPI
    from fastapi.responses import JSONResponse
except ImportError:  # pragma: no cover — guest / no FastAPI
    FastAPI = None
    JSONResponse = None


class FastAPIAdapter:
    def __init__(self, role="cdn", theme="cdn", app=None, *, include_health=True):
        if FastAPI is None:
            raise RuntimeError("fastapi not available")
        self.role = role
        self.theme = theme
        self.include_health = include_health
        self.app = app if app is not None else FastAPI()
        self._register()

    def capabilities(self):
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
        adapter = self

        if self.include_health:

            @app.get("/health")
            async def health():
                return {"ok": True}

        @app.get("/capabilities")
        async def capabilities():
            return adapter.capabilities()

        @app.get("/inspect/self")
        async def inspect_self():
            return adapter.self_desc()

        for method, path, implemented in ENDPOINT_STUBS:
            if implemented:
                continue

            async def not_impl(p=path):
                return JSONResponse(
                    {"error": "NotImplemented", "path": p},
                    status_code=501,
                )

            app.add_api_route(path, not_impl, methods=[method])
