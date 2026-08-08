"""Shared Inspect endpoint contract (guest Microdot + CDN FastAPI)."""

from .self_desc import self_description

CAP_DEFAULTS = {
    "smp": True,
    "asgi": True,
    "ssh_kex": True,
    "ssh_auth": True,
    "microdot": True,
    # Product LIVE has no Rust VFS linked — static is ASGI embed.
    "vfs_static": False,
    "static_embed": True,
}


def capabilities(role, theme, *, fastapi=False, **extra):
    caps = dict(CAP_DEFAULTS)
    caps["role"] = role
    caps["theme"] = theme
    caps["fastapi"] = bool(fastapi)
    if role == "cdn":
        caps["microdot"] = False
        caps["fastapi"] = True
        caps["vfs_static"] = False
        caps["static_embed"] = False
    caps.update(extra)
    return caps


# method, path, implemented (False → 501 NotImplemented stub)
ENDPOINT_STUBS = (
    ("GET", "/health", True),
    ("GET", "/capabilities", True),
    ("GET", "/inspect/self", True),
)

__all__ = [
    "CAP_DEFAULTS",
    "ENDPOINT_STUBS",
    "capabilities",
    "self_description",
]
