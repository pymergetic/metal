from httpd.util import doc_list_row_esc, doc_row_esc, esc, html, json_page, json_response, paginate


def register(app):
  @app.get("/docs")
  async def docs_list(request):
    import pymergetic.metal.doc as doc

    kind = request.args.get("kind")
    raw, pager = paginate(doc.list(kind), request, default=50)
    rows = [doc_list_row_esc(r) for r in raw]
    return html(
        "docs_list.html",
        title="docs",
        kind=esc(kind) if kind else "",
        rows=rows,
        pager=pager,
    )

  @app.get("/docs/key/<doc_key>")
  async def docs_key(request, doc_key):
    import pymergetic.metal.doc as doc

    _ = request
    row = doc.lookup_key(doc_key)
    if row is None:
      return json_response({"error": "not found"}, 404)
    er = doc_row_esc(row)
    return html("docs_detail.html", title=er["key"], row=er)

  @app.get("/docs/<kind>/<key>")
  async def docs_detail(request, kind, key):
    import pymergetic.metal.doc as doc

    _ = request
    row = doc.lookup(kind, key)
    if row is None:
      return json_response({"error": "not found"}, 404)
    er = doc_row_esc(row)
    return html("docs_detail.html", title=er["key"], row=er)

  @app.get("/api/doc")
  async def api_doc_list(request):
    import pymergetic.metal.doc as doc

    kind = request.args.get("kind")
    rows, pager = paginate(doc.list(kind), request, default=50)
    return json_page(rows, pager)

  @app.get("/api/doc/key/<doc_key>")
  async def api_doc_lookup_key(request, doc_key):
    import pymergetic.metal.doc as doc

    _ = request
    row = doc.lookup_key(doc_key)
    if row is None:
      return json_response({"error": "not found"}, 404)
    return json_response(row)

  @app.get("/api/doc/<kind>/<key>")
  async def api_doc_lookup(request, kind, key):
    import pymergetic.metal.doc as doc

    _ = request
    row = doc.lookup(kind, key)
    if row is None:
      return json_response({"error": "not found"}, 404)
    return json_response(row)
