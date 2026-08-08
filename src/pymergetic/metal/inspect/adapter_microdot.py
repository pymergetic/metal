"""Route adapter: Inspect stubs → Microdot (framework-independent registry)."""

from .stubs import ENDPOINT_STUBS, capabilities as make_capabilities, self_description

try:
    from microdot import Microdot
except ImportError:  # pragma: no cover — host tooling
    Microdot = None


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

        for method, path, implemented in ENDPOINT_STUBS:
            if implemented:
                continue

            @app.route(path, methods=[method])
            async def not_impl(request, p=path):
                # Tuple form avoids Response/NoCaseDict (dict subclass attrs
                # break under MICROPY_CONFIG_ROM_LEVEL_MINIMUM).
                return {"error": "NotImplemented", "path": p}, 501
