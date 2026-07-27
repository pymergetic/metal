# Metal ASGI <-> Microdot bridge (guest Python).
#
# C ASGI owns sockets; handle() is the Py runner leaf for py:microdot mounts.
# asyncio is Metal's cooperative shim (stdlib.zip) over pymergetic.metal.aio —
# microdot's `import asyncio` is real, not a dodge.

from microdot import Microdot
from microdot import Request
from microdot import Response
from microdot.microdot import NoCaseDict

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


def _json_response(obj, status=200):
  import json

  return json.dumps(obj), status, {"Content-Type": "application/json"}


# --- docs/DOC_IFACE_PLAN.md Part III (thin JSON add) -----------------------
# Every route below is a straight pass-through onto pymergetic.metal.doc /
# pymergetic.metal.iface (Part I / Part II's own catalogs) -- no third copy
# of doc text or the sym table lives here.


@app.get("/api/doc")
async def api_doc_list(request):
  import pymergetic.metal.doc as doc

  kind = request.args.get("kind")
  return _json_response(doc.list(kind))


@app.get("/api/doc/key/<doc_key>")
async def api_doc_lookup_key(request, doc_key):
  import pymergetic.metal.doc as doc

  _ = request
  row = doc.lookup_key(doc_key)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  return _json_response(row)


@app.get("/api/doc/<kind>/<key>")
async def api_doc_lookup(request, kind, key):
  import pymergetic.metal.doc as doc

  _ = request
  row = doc.lookup(kind, key)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  return _json_response(row)


@app.get("/api/iface")
async def api_iface_info(request):
  import pymergetic.metal.iface as iface

  _ = request
  return _json_response(iface.info())


@app.get("/api/iface/pkg")
async def api_iface_pkg_list(request):
  import pymergetic.metal.iface as iface

  _ = request
  return _json_response(iface.list())


@app.get("/api/iface/pkg/<name>")
async def api_iface_pkg_one(request, name):
  import pymergetic.metal.iface as iface

  _ = request
  info = iface.info()
  if name not in info:
    return _json_response({"error": "unknown package"}, 404)
  return _json_response(info[name])


@app.get("/api/iface/pkg/<name>/files")
async def api_iface_pkg_files(request, name):
  import pymergetic.metal.iface as iface

  _ = request
  try:
    return _json_response(iface.list(name))
  except ValueError:
    return _json_response({"error": "unknown package"}, 404)


@app.get("/api/iface/pkg/<name>/file/<path:path>")
async def api_iface_pkg_file(request, name, path):
  import pymergetic.metal.iface as iface

  _ = request
  try:
    body = iface.read(name, path)
  except ValueError:
    return _json_response({"error": "not found"}, 404)
  try:
    text = body.decode()
  except Exception:
    return body, 200, {"Content-Type": "application/octet-stream"}
  return text, 200, {"Content-Type": "text/plain"}


@app.get("/api/iface/sym")
async def api_iface_sym_query(request):
  import pymergetic.metal.iface as iface

  module = request.args.get("module")
  name = request.args.get("name")
  if not module or not name:
    return _json_response({"error": "module and name query params required"}, 400)
  row = iface.sym(module, name)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  return _json_response(row)


@app.get("/api/iface/sym/<module>/<name>")
async def api_iface_sym_one(request, module, name):
  import pymergetic.metal.iface as iface

  _ = request
  row = iface.sym(module, name)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  return _json_response(row)


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
