"""pymergetic.metal.net.microdot — vendored upstream Microdot (v2.6.2) + project face.

This is the real miguelgrinberg/microdot core (see `microdot.py`), re-exported
here so `pymergetic.metal.net.microdot.Microdot` is the genuine framework with
`Request`, route params and `Response`. One project extension is added on top:
`Microdot.attach_asgi()` — mount the same routes on the metal `net.http.asgi`
(ln) server via one shared asgi face, rather than a second socket.

MicrodotShell / FastAPIShell mount identical `pymergetic.metal.inspect` handlers
on this app. Swap the shell and the handlers stay the same.
"""

from .microdot import (  # noqa: F401
    Microdot,
    Request,
    Response,
    URLPattern,
    AsyncBytesIO,
    HTTPException,
    MultiDict,
    NoCaseDict,
    abort,
    redirect,
    send_file,
    iscoroutine,
)

__version__ = "2.6.2"


def _asgi(self):
    """Return the shared metal `net.http.asgi` face (no second socket)."""
    import pymergetic.metal.net.http.asgi as asgi

    return asgi


# Project extension: the metal asgi/ln server is the single listen. The real
# Microdot has no `attach_asgi`; add it here so handlers registered on this app
# can also be served by the C/Rust server behind the same API contract.
Microdot.attach_asgi = _asgi
