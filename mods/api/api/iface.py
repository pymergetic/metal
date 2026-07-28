from httpd.highlight import highlight_c
from httpd.util import (
    doc_row_esc,
    esc,
    html,
    iface_norm_path,
    json_page,
    json_response,
    paginate,
    sym_row_esc,
    url_query_val,
)


def _iface_is_prose_path(path):
  if path is None:
    return False
  p = str(path)
  if p == "LICENSE" or p.endswith("/LICENSE"):
    return True
  base = p.rsplit("/", 1)[-1]
  return base.endswith(".md")


async def _iface_file_page(name, path):
  import pymergetic.metal.iface as iface

  path = iface_norm_path(path)
  if not path:
    return json_response({"error": "not found"}, 404)
  try:
    body = iface.read(name, path)
  except ValueError:
    return json_response({"error": "not found"}, 404)
  try:
    text = body.decode()
  except Exception:
    text = ""
  if _iface_is_prose_path(path):
    body_html = esc(text)
  else:
    body_html = highlight_c(text)
  return html(
      "iface_file.html",
      title=esc(path),
      name=esc(name),
      path=esc(path),
      body_html=body_html,
  )


def register(app):
  @app.get("/iface")
  async def iface_home(request):
    import pymergetic.metal.iface as iface

    _ = request
    info = iface.info()
    pkgs = []
    for name in sorted(info.keys()):
      pkgs.append((esc(name), {
          "kind": esc(info[name].get("kind", "")),
          "version": esc(info[name].get("version", "")),
          "nfiles": info[name].get("nfiles", 0),
          "blob_len": info[name].get("blob_len", 0),
      }))
    return html("iface_list.html", title="iface", pkgs=pkgs)

  @app.get("/iface/pkg/<name>")
  async def iface_pkg(request, name):
    import pymergetic.metal.iface as iface

    _ = request
    info = iface.info()
    if name not in info:
      return json_response({"error": "unknown package"}, 404)
    files = []
    for p in iface.list(name):
      np = iface_norm_path(p)
      files.append({
          "path": esc(np),
          "href": "/iface/pkg/" + name + "/view?path=" + url_query_val(np),
      })
    meta = info[name]
    return html(
        "iface_pkg.html",
        title=esc(name),
        name=esc(name),
        info={
            "kind": esc(meta.get("kind", "")),
            "version": esc(meta.get("version", "")),
            "nfiles": meta.get("nfiles", 0),
            "blob_len": meta.get("blob_len", 0),
        },
        files=files,
    )

  @app.get("/iface/pkg/<name>/view")
  async def iface_file_view(request, name):
    path = request.args.get("path") if request is not None else None
    if path is None:
      return json_response({"error": "missing path"}, 400)
    return await _iface_file_page(name, path)

  @app.get("/iface/pkg/<name>/file/<path:path>")
  async def iface_file(request, name, path):
    _ = request
    return await _iface_file_page(name, path)

  @app.get("/iface/sym")
  async def iface_sym_list(request):
    import pymergetic.metal.iface as iface

    module = request.args.get("module")
    if module:
      rows = iface.sym(module)
    else:
      rows = iface.sym()
    rows, pager = paginate([sym_row_esc(r) for r in rows], request, default=50)
    return html(
        "iface_sym_list.html",
        title="syms",
        module=esc(module) if module else "",
        rows=rows,
        pager=pager,
    )

  @app.get("/iface/sym/<module>/<name>")
  async def iface_sym_detail(request, module, name):
    import pymergetic.metal.doc as doc
    import pymergetic.metal.iface as iface

    _ = request
    row = iface.sym(module, name)
    if row is None:
      return json_response({"error": "not found"}, 404)
    d = None
    doc_key = row.get("doc_key")
    if doc_key:
      linked = doc.lookup_key(doc_key)
      if linked is not None:
        d = doc_row_esc(linked)
    er = sym_row_esc(row)
    return html("iface_sym_detail.html", title=er["name"], row=er, doc=d)

  @app.get("/api/iface")
  async def api_iface_info(request):
    import pymergetic.metal.iface as iface

    _ = request
    return json_response(iface.info())

  @app.get("/api/iface/pkg")
  async def api_iface_pkg_list(request):
    import pymergetic.metal.iface as iface

    _ = request
    return json_response(iface.list())

  @app.get("/api/iface/pkg/<name>")
  async def api_iface_pkg_one(request, name):
    import pymergetic.metal.iface as iface

    _ = request
    info = iface.info()
    if name not in info:
      return json_response({"error": "unknown package"}, 404)
    return json_response(info[name])

  @app.get("/api/iface/pkg/<name>/files")
  async def api_iface_pkg_files(request, name):
    import pymergetic.metal.iface as iface

    _ = request
    try:
      return json_response(iface.list(name))
    except ValueError:
      return json_response({"error": "unknown package"}, 404)

  @app.get("/api/iface/pkg/<name>/file")
  async def api_iface_pkg_file_q(request, name):
    import pymergetic.metal.iface as iface

    path = request.args.get("path") if request is not None else None
    if path is None:
      return json_response({"error": "missing path"}, 400)
    path = iface_norm_path(path)
    try:
      body = iface.read(name, path)
    except ValueError:
      return json_response({"error": "not found"}, 404)
    try:
      text = body.decode()
    except Exception:
      return body, 200, {"Content-Type": "application/octet-stream"}
    return text, 200, {"Content-Type": "text/plain; charset=utf-8"}

  @app.get("/api/iface/pkg/<name>/file/<path:path>")
  async def api_iface_pkg_file(request, name, path):
    import pymergetic.metal.iface as iface

    _ = request
    path = iface_norm_path(path)
    try:
      body = iface.read(name, path)
    except ValueError:
      return json_response({"error": "not found"}, 404)
    try:
      text = body.decode()
    except Exception:
      return body, 200, {"Content-Type": "application/octet-stream"}
    return text, 200, {"Content-Type": "text/plain; charset=utf-8"}

  @app.get("/api/iface/sym")
  async def api_iface_sym_query(request):
    import pymergetic.metal.iface as iface

    module = request.args.get("module")
    name = request.args.get("name")
    if module and name:
      row = iface.sym(module, name)
      if row is None:
        return json_response({"error": "not found"}, 404)
      return json_response(row)
    if module:
      rows, pager = paginate(iface.sym(module), request, default=50)
      return json_page(rows, pager)
    rows, pager = paginate(iface.sym(), request, default=50)
    return json_page(rows, pager)

  @app.get("/api/iface/sym/<module>/<name>")
  async def api_iface_sym_one(request, module, name):
    import pymergetic.metal.iface as iface

    _ = request
    row = iface.sym(module, name)
    if row is None:
      return json_response({"error": "not found"}, 404)
    return json_response(row)
