"""Route adapter: Inspect stubs → Microdot (framework-independent registry)."""

try:
    from microdot import Microdot, Response
except ImportError:  # pragma: no cover — host tooling
    Microdot = None
    Response = None


class MicrodotAdapter:
    def __init__(self, role="metal", theme="metal"):
        if Microdot is None:
            raise RuntimeError("microdot not available")
        self.role = role
        self.theme = theme
        self.app = Microdot()
        self._register()

    def capabilities(self):
        return {
            "role": self.role,
            "theme": self.theme,
            "smp": True,
            "asgi": True,
            "ssh_kex": True,
            "ssh_auth": True,
            "fastapi": False,
            "microdot": True,
            "vfs_static": True,
        }

    def _register(self):
        app = self.app

        @app.get("/health")
        async def health(request):
            return {"ok": True}

        @app.get("/capabilities")
        async def capabilities(request):
            return self.capabilities()

        @app.get("/inspect/self")
        async def inspect_self(request):
            return Response(
                {"error": "NotImplemented", "path": "/inspect/self"},
                status_code=501,
            )
