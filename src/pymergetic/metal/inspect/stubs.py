"""Shared Inspect endpoint contract (guest Microdot + CDN FastAPI)."""

from .self_desc import self_description

CAP_DEFAULTS = {
    "smp": True,
    "asgi": True,
    "ssh_kex": False,
    "ssh_auth": False,
    # Py face mounts on ASGI; not a second listen. fastapi only when role=cdn.
    "microdot": True,
    "fastapi": False,
    # www/ not on the wire until ASGI route_static.
    "vfs_static": False,
    "static_embed": False,
    "static_backend": "none",
}


def capabilities(role, theme, *, fastapi=False, **extra):
    caps = dict(CAP_DEFAULTS)
    caps["role"] = role
    caps["theme"] = theme
    if role == "cdn":
        caps["microdot"] = False
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
)

__all__ = [
    "CAP_DEFAULTS",
    "ENDPOINT_STUBS",
    "capabilities",
    "self_description",
]
