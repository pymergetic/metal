"""Shared Inspect endpoint contract (guest Microdot + CDN FastAPI)."""

from .self_desc import self_description

CAP_DEFAULTS = {
    "smp": True,
    "asgi": True,
    "ssh_kex": True,
    "ssh_auth": False,
    # Py face mounts on ASGI; not a second listen. fastapi only when role=cdn.
    "microdot": True,
    "fastapi": False,
    "utemplate": True,  # vendored pfalcon/utemplate engine (microdot server-side templates)
    # www/ is route_static of embedded bytes (firmware cannot fopen).
    "vfs_static": False,
    "static_embed": True,
    "static_backend": "embed",
    # Registry modules + funcs are exposed; RPC invokes container (wasm/aot/elf)
    # exports over the wire. Resident native C/Rust exports are refused.
    "rpc": True,
    "rpc_i64": True,
    "rpc_f32": False,
    "rpc_f64": False,
}


def capabilities(role, theme, *, fastapi=False, **extra):
    caps = dict(CAP_DEFAULTS)
    caps["role"] = role
    caps["theme"] = theme
    if role == "cdn":
        caps["microdot"] = False
        caps["utemplate"] = False
        caps["fastapi"] = True
    else:
        caps["fastapi"] = bool(fastapi)
    caps.update(extra)
    return caps


# method, path, implemented (False → 501 NotImplemented stub)
ENDPOINT_STUBS = (
    ("GET", "/health", True),
    ("GET", "/capabilities", True),
    ("GET", "/inspect/self", True),
    ("GET", "/inspect/reg", True),
    ("GET", "/inspect/reg/seats", True),
    ("GET", "/inspect/reg/completeness", True),
    ("GET", "/inspect/reg/method", True),
    ("GET", "/inspect/reg/<module>", True),
    ("GET", "/inspect/reg/<module>/<method>", True),
    # RPC: invoke a container (wasm/aot/elf) export with scalar args in query.
    ("GET", "/inspect/call/<module>/<method>", True),
    # Package catalog + version nav for the shared Inspect commander. The seat
    # serves these from the live registry (artifacts.py, deferred pump); the CDN
    # from its database — same wire contract, so `implemented` on both.
    ("GET", "/packages", True),
    ("GET", "/packages/{name}", True),
    ("GET", "/packages/{name}/versions", True),
    # CDN-shaped artifact introspect API (a card artifact -> inspect/sections/
    # symbols/disasm/addr2line/locations/files). Same shapes both seats answer.
    ("GET", "/artifacts/lead/{artifact}/inspect", True),
    ("GET", "/artifacts/lead/{artifact}/sections", True),
    ("GET", "/artifacts/lead/{artifact}/symbols", True),
    ("GET", "/artifacts/lead/{artifact}/disasm", True),
    ("GET", "/artifacts/lead/{artifact}/addr2line", True),
    ("GET", "/artifacts/lead/{artifact}/locations", True),
    ("GET", "/artifacts/lead/{artifact}/files", True),
    ("GET", "/artifacts/lead/{artifact}/files/raw", True),
    ("GET", "/artifacts/lead/{artifact}/sections/raw", True),
    # FastAPI-style interactive API docs (swagger-ui over /openapi.json).
    ("GET", "/openapi.json", True),
    ("GET", "/docs", True),
)

__all__ = [
    "CAP_DEFAULTS",
    "ENDPOINT_STUBS",
    "capabilities",
    "self_description",
]
