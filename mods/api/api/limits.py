from util import html, json_page, json_response, lim_esc, paginate


def register(app):
  @app.get("/limits")
  async def limits_home(request):
    import pymergetic.metal.mem.limit as mem_limit

    rows, pager = paginate(
        [lim_esc(r) for r in mem_limit.list()], request, default=50
    )
    return html("limits_list.html", title="limits", rows=rows, pager=pager)

  @app.get("/limits/<lim_id>")
  async def limits_detail(request, lim_id):
    import pymergetic.metal.mem.limit as mem_limit

    _ = request
    row = mem_limit.get(lim_id)
    if row is None:
      return json_response({"error": "not found"}, 404)
    er = lim_esc(row)
    return html("limits_detail.html", title=er["id"], row=er)

  @app.get("/api/limits")
  async def api_limits_list(request):
    import pymergetic.metal.mem.limit as mem_limit

    rows, pager = paginate(mem_limit.list(), request, default=50)
    return json_page(rows, pager)

  @app.get("/api/limits/<lim_id>")
  async def api_limits_one(request, lim_id):
    import pymergetic.metal.mem.limit as mem_limit

    _ = request
    row = mem_limit.get(lim_id)
    if row is None:
      return json_response({"error": "not found"}, 404)
    return json_response(row)
