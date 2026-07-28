from util import ext_esc, html, json_page, json_response, paginate


def register(app):
  @app.get("/externals")
  async def externals_home(request):
    import pymergetic.metal.externals as externals

    rows, pager = paginate(
        [ext_esc(r) for r in externals.list()], request, default=50
    )
    return html("externals_list.html", title="externals", rows=rows, pager=pager)

  @app.get("/externals/<ext_id>")
  async def externals_detail(request, ext_id):
    import pymergetic.metal.externals as externals

    _ = request
    row = externals.get(ext_id)
    if row is None:
      return json_response({"error": "not found"}, 404)
    er = ext_esc(row)
    return html("externals_detail.html", title=er["id"], row=er)

  @app.get("/api/externals")
  async def api_externals_list(request):
    import pymergetic.metal.externals as externals

    rows, pager = paginate(externals.list(), request, default=50)
    return json_page(rows, pager)

  @app.get("/api/externals/<ext_id>")
  async def api_externals_one(request, ext_id):
    import pymergetic.metal.externals as externals

    _ = request
    row = externals.get(ext_id)
    if row is None:
      return json_response({"error": "not found"}, 404)
    return json_response(row)
