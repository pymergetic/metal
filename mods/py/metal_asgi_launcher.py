# Metal ASGI <-> Microdot bridge (guest Python).
#
# C ASGI owns sockets; handle() is the Py runner leaf for py:microdot mounts.
# asyncio is Metal's cooperative shim (stdlib.zip) over pymergetic.metal.aio —
# microdot's `import asyncio` is real, not a dodge.
#
# Part III (docs/DOC_IFACE_PLAN.md): JSON under /api/* plus utemplate HTML
# pages for the same doc/iface catalogs (templates/ + compiled.Loader).

from microdot import Microdot
from microdot import Request
from microdot import Response
from microdot.microdot import NoCaseDict
from microdot.utemplate import Template
from utemplate import compiled

app = Microdot()
Response.default_content_type = "text/html"
Template.initialize(template_dir="templates", loader_class=compiled.Loader)


def _json_response(obj, status=200):
  import json

  return json.dumps(obj), status, {"Content-Type": "application/json"}


def _html(tpl, **kwargs):
  return Template(tpl).render(**kwargs), 200, {"Content-Type": "text/html"}


def _esc(s):
  if s is None:
    return ""
  if not isinstance(s, str):
    try:
      s = s.decode()
    except Exception:
      s = str(s)
  return (
      s.replace("&", "&amp;")
      .replace("<", "&lt;")
      .replace(">", "&gt;")
      .replace('"', "&quot;")
  )


def _doc_row_esc(row):
  return {
      "kind": _esc(row.get("kind", "")),
      "key": _esc(row.get("key", "")),
      "summary": _esc(row.get("summary", "")),
      "sig": _esc(row.get("sig", "")),
      "body": _esc(row.get("body", "")),
  }


def _sym_row_esc(row):
  return {
      "module": _esc(row.get("module", "")),
      "name": _esc(row.get("name", "")),
      "sig": _esc(row.get("sig", "")),
      "class_": row.get("class_", 0),
      "doc_key": _esc(row.get("doc_key", "")),
  }


def _about_esc(about):
  authors = []
  for a in about.get("authors") or []:
    authors.append({
        "name": _esc(a.get("name", "")),
        "email": _esc(a.get("email", "")),
        "role": _esc(a.get("role", "")),
    })
  return {
      "version": _esc(about.get("version", "")),
      "desc": _esc(about.get("desc", "")),
      "url": _esc(about.get("url", "")),
      "authors": authors,
  }


def _ext_esc(row):
  return {
      "id": _esc(row.get("id", "")),
      "version": _esc(row.get("version", "")),
      "url": _esc(row.get("url", "")),
      "note": _esc(row.get("note", "")),
  }


def _lim_esc(row):
  return {
      "id": _esc(row.get("id", "")),
      "module": _esc(row.get("module", "")),
      "name": _esc(row.get("name", "")),
      "value": row.get("value", 0),
      "unit": _esc(row.get("unit", "")),
      "note": _esc(row.get("note", "")),
  }


def _metal_version():
  try:
    import pymergetic.metal.authors as authors

    about = authors.about()
    if isinstance(about, dict) and about.get("version"):
      return str(about["version"])
  except Exception:
    pass
  try:
    import pymergetic.metal.iface as iface

    info = iface.info()
    if "metal.guest" in info and info["metal.guest"].get("version"):
      return str(info["metal.guest"]["version"])
  except Exception:
    pass
  return "metal"


def _limit_rows(rows, request, default=40):
  """Cap list payloads — asgi conn_send is sync and aborts large bodies mid-way."""
  lim = default
  raw = request.args.get("limit") if request is not None else None
  if raw is not None:
    try:
      lim = int(raw)
    except Exception:
      lim = default
  if lim < 0:
    lim = 0
  if lim > 500:
    lim = 500
  if len(rows) <= lim:
    return rows, False
  return rows[:lim], True


@app.get("/health")
async def health(request):
  _ = request
  return "ok\n", 200, {"Content-Type": "text/plain"}


# --- HTML (Part III-H2) ----------------------------------------------------


@app.get("/")
async def index(request):
  import pymergetic.metal.doc as doc
  import pymergetic.metal.externals as externals
  import pymergetic.metal.iface as iface
  import pymergetic.metal.mem.limit as mem_limit

  _ = request
  docs = doc.list()
  pkgs = iface.list()
  syms = iface.sym()
  return _html(
      "index.html",
      title="home",
      version=_esc(_metal_version()),
      n_docs=len(docs),
      n_pkgs=len(pkgs),
      n_syms=len(syms),
      n_ext=len(externals.list()),
      n_lim=len(mem_limit.list()),
  )


@app.get("/docs")
async def docs_list(request):
  import pymergetic.metal.doc as doc

  kind = request.args.get("kind")
  rows, truncated = _limit_rows([_doc_row_esc(r) for r in doc.list(kind)], request)
  _ = truncated
  return _html(
      "docs_list.html",
      title="docs",
      kind=_esc(kind) if kind else "",
      rows=rows,
  )


@app.get("/docs/key/<doc_key>")
async def docs_key(request, doc_key):
  import pymergetic.metal.doc as doc

  _ = request
  row = doc.lookup_key(doc_key)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  er = _doc_row_esc(row)
  return _html("docs_detail.html", title=er["key"], row=er)


@app.get("/docs/<kind>/<key>")
async def docs_detail(request, kind, key):
  import pymergetic.metal.doc as doc

  _ = request
  row = doc.lookup(kind, key)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  er = _doc_row_esc(row)
  return _html("docs_detail.html", title=er["key"], row=er)


@app.get("/iface")
async def iface_home(request):
  import pymergetic.metal.iface as iface

  _ = request
  info = iface.info()
  pkgs = []
  for name in sorted(info.keys()):
    pkgs.append((_esc(name), {
        "kind": _esc(info[name].get("kind", "")),
        "version": _esc(info[name].get("version", "")),
        "nfiles": info[name].get("nfiles", 0),
        "blob_len": info[name].get("blob_len", 0),
    }))
  return _html("iface_list.html", title="iface", pkgs=pkgs)


@app.get("/iface/pkg/<name>")
async def iface_pkg(request, name):
  import pymergetic.metal.iface as iface

  _ = request
  info = iface.info()
  if name not in info:
    return _json_response({"error": "unknown package"}, 404)
  files = [_esc(p) for p in iface.list(name)]
  meta = info[name]
  return _html(
      "iface_pkg.html",
      title=_esc(name),
      name=_esc(name),
      info={
          "kind": _esc(meta.get("kind", "")),
          "version": _esc(meta.get("version", "")),
          "nfiles": meta.get("nfiles", 0),
          "blob_len": meta.get("blob_len", 0),
      },
      files=files,
  )


@app.get("/iface/pkg/<name>/file/<path:path>")
async def iface_file(request, name, path):
  import pymergetic.metal.iface as iface

  _ = request
  try:
    body = iface.read(name, path)
  except ValueError:
    return _json_response({"error": "not found"}, 404)
  try:
    text = body.decode()
  except Exception:
    text = ""
  return _html(
      "iface_file.html",
      title=_esc(path),
      name=_esc(name),
      path=_esc(path),
      text=_esc(text),
  )


@app.get("/iface/sym")
async def iface_sym_list(request):
  import pymergetic.metal.iface as iface

  module = request.args.get("module")
  if module:
    rows = iface.sym(module)
  else:
    rows = iface.sym()
  rows, _truncated = _limit_rows([_sym_row_esc(r) for r in rows], request, default=40)
  return _html(
      "iface_sym_list.html",
      title="syms",
      module=_esc(module) if module else "",
      rows=rows,
  )


@app.get("/iface/sym/<module>/<name>")
async def iface_sym_detail(request, module, name):
  import pymergetic.metal.doc as doc
  import pymergetic.metal.iface as iface

  _ = request
  row = iface.sym(module, name)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  d = None
  doc_key = row.get("doc_key")
  if doc_key:
    linked = doc.lookup_key(doc_key)
    if linked is not None:
      d = _doc_row_esc(linked)
  er = _sym_row_esc(row)
  return _html("iface_sym_detail.html", title=er["name"], row=er, doc=d)


@app.get("/about")
async def about_page(request):
  import pymergetic.metal.authors as authors

  _ = request
  about = authors.about()
  if not isinstance(about, dict):
    return _json_response({"error": "unavailable"}, 503)
  er = _about_esc(about)
  return _html("about.html", title="about", about=er)


@app.get("/externals")
async def externals_home(request):
  import pymergetic.metal.externals as externals

  rows, _truncated = _limit_rows(
      [_ext_esc(r) for r in externals.list()], request, default=80
  )
  return _html("externals_list.html", title="externals", rows=rows)


@app.get("/externals/<ext_id>")
async def externals_detail(request, ext_id):
  import pymergetic.metal.externals as externals

  _ = request
  row = externals.get(ext_id)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  er = _ext_esc(row)
  return _html("externals_detail.html", title=er["id"], row=er)


@app.get("/limits")
async def limits_home(request):
  import pymergetic.metal.mem.limit as mem_limit

  rows, _truncated = _limit_rows(
      [_lim_esc(r) for r in mem_limit.list()], request, default=80
  )
  return _html("limits_list.html", title="limits", rows=rows)


@app.get("/limits/<lim_id>")
async def limits_detail(request, lim_id):
  import pymergetic.metal.mem.limit as mem_limit

  _ = request
  row = mem_limit.get(lim_id)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  er = _lim_esc(row)
  return _html("limits_detail.html", title=er["id"], row=er)


# --- JSON (Part III-H1) ----------------------------------------------------


@app.get("/api/doc")
async def api_doc_list(request):
  import pymergetic.metal.doc as doc

  kind = request.args.get("kind")
  rows, _truncated = _limit_rows(doc.list(kind), request, default=40)
  return _json_response(rows)


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
  if module and name:
    row = iface.sym(module, name)
    if row is None:
      return _json_response({"error": "not found"}, 404)
    return _json_response(row)
  if module:
    return _json_response(iface.sym(module))
  return _json_response(iface.sym())


@app.get("/api/iface/sym/<module>/<name>")
async def api_iface_sym_one(request, module, name):
  import pymergetic.metal.iface as iface

  _ = request
  row = iface.sym(module, name)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  return _json_response(row)


@app.get("/api/about")
async def api_about(request):
  import pymergetic.metal.authors as authors

  _ = request
  about = authors.about()
  if not isinstance(about, dict):
    return _json_response({"error": "unavailable"}, 503)
  return _json_response(about)


@app.get("/api/externals")
async def api_externals_list(request):
  import pymergetic.metal.externals as externals

  rows, _truncated = _limit_rows(externals.list(), request, default=80)
  return _json_response(rows)


@app.get("/api/externals/<ext_id>")
async def api_externals_one(request, ext_id):
  import pymergetic.metal.externals as externals

  _ = request
  row = externals.get(ext_id)
  if row is None:
    return _json_response({"error": "not found"}, 404)
  return _json_response(row)


@app.get("/api/limits")
async def api_limits_list(request):
  import pymergetic.metal.mem.limit as mem_limit

  rows, _truncated = _limit_rows(mem_limit.list(), request, default=80)
  return _json_response(rows)


@app.get("/api/limits/<lim_id>")
async def api_limits_one(request, lim_id):
  import pymergetic.metal.mem.limit as mem_limit

  _ = request
  row = mem_limit.get(lim_id)
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
