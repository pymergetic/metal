"""In-process Microdot dispatch for C ASGI (no Py socket server).

Avoids Microdot Response/NoCaseDict: dict subclasses cannot take attributes
under this port's MINIMUM ROM level (AttributeError in Response.__init__).
"""

import asyncio
import json

from microdot import Request

from .app import create_app

_app = None


async def _invoke(handler, req):
    ret = handler(req)
    if hasattr(ret, "send") and hasattr(ret, "throw"):
        ret = await ret
    return ret


def handle(method, path):
    """Dispatch Inspect JSON route.

    Returns (status_code, body_str) or None if the path is not an Inspect
    route (so C ASGI can serve static /inspect or 404).
    """
    global _app
    if _app is None:
        _app = create_app()
    req = Request(_app, ("127.0.0.1", 0), method, path, "1.0", {})
    f, _prefix, _sub = _app.find_route(req)
    if not callable(f):
        return None
    raw = asyncio.run(_invoke(f, req))
    status = 200
    body = raw
    if isinstance(raw, tuple):
        body = raw[0]
        if len(raw) > 1 and isinstance(raw[1], int):
            status = raw[1]
        elif len(raw) > 1 and isinstance(raw[0], int):
            status = raw[0]
            body = ""
    if isinstance(body, dict) or isinstance(body, list):
        # Compact JSON matches prior C stubs / live-http greps.
        body = json.dumps(body, separators=(",", ":"))
    elif body is None:
        body = ""
    elif isinstance(body, bytes):
        body = str(body, "utf-8")
    elif not isinstance(body, str):
        body = str(body)
    return status, body
