# Metal ASGI <-> Microdot bridge (guest Python).
#
# C ASGI owns sockets; handle() is the Py runner leaf for py:httpd mounts.
# asyncio is Metal's cooperative shim (loose /mods/py/stdlib) over pymergetic.metal.aio.
# Domain routes live under mods/api/ (imported as package api).

from microdot import Microdot
from microdot import Request
from microdot import Response
from microdot.microdot import NoCaseDict
from microdot.utemplate import Template
from utemplate import compiled

import api

app = Microdot()
Response.default_content_type = "text/html"
Template.initialize(template_dir="templates", loader_class=compiled.Loader)  # type: ignore[arg-type]
api.register(app)


async def handle(conn_id):
  """Async entry for pm_metal_py_fn_call_async (one int arg)."""
  import pymergetic.metal.net.asgi as asgi

  _ = conn_id
  method = asgi.conn_method()
  path = asgi.conn_path()
  if method is None or path is None:
    asgi.reply(500, "text/plain", "no conn\n")
    return 0

  headers = NoCaseDict()
  req = Request(app, ("0.0.0.0", 0), method, path, "1.1", headers, body=b"")
  res = await app.dispatch_request(req)
  if res is None or res is Response.already_handled:
    asgi.reply(500, "text/plain", "no response\n")
    return -1
  body = res.body
  if body is None:
    body = b""
  ctype = "text/html"
  try:
    if "Content-Type" in res.headers:
      ctype = res.headers["Content-Type"]
  except Exception:
    pass
  if isinstance(body, (bytes, bytearray)):
    try:
      text = body.decode()
    except Exception:
      text = ""
  else:
    text = str(body)
  asgi.reply(int(res.status_code), ctype, text)
  return 0
