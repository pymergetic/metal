# Metal ASGI <-> Microdot bridge (guest Python).
#
# C ASGI owns sockets; handle() is the Py runner leaf for py:microdot mounts.
# asyncio is Metal's cooperative shim (stdlib.zip) over pymergetic.metal.aio —
# microdot's `import asyncio` is real, not a dodge.

from microdot import Microdot
from microdot.microdot import NoCaseDict
from microdot.microdot import Request
from microdot.microdot import Response

app = Microdot()


def sysinfo_json():
  import sys

  ver = sys.version
  sp = ver.find(" ")
  if sp > 0:
    ver = ver[:sp]
  sc = ver.find(";")
  if sc > 0:
    ver = ver[:sc]
  return (
      '{"runtime":"metal","python":"'
      + ver
      + '","app":"microdot"}\n'
  )


@app.get("/")
async def index(request):
  _ = request
  return sysinfo_json(), 200, {"Content-Type": "application/json"}


@app.get("/health")
async def health(request):
  _ = request
  return "ok\n", 200, {"Content-Type": "text/plain"}


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
  ctype = "application/json"
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
