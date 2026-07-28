from httpd.util import about_esc, esc, html, json_response, metal_version


def register(app):
  @app.get("/health")
  async def health(request):
    _ = request
    return "ok\n", 200, {"Content-Type": "text/plain"}

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
    return html(
        "index.html",
        title="home",
        version=esc(metal_version()),
        n_docs=len(docs),
        n_pkgs=len(pkgs),
        n_syms=len(syms),
        n_ext=len(externals.list()),
        n_lim=len(mem_limit.list()),
    )

  @app.get("/about")
  async def about_page(request):
    import pymergetic.metal.authors as authors

    _ = request
    about = authors.about()
    if not isinstance(about, dict):
      return json_response({"error": "unavailable"}, 503)
    er = about_esc(about)
    return html("about.html", title="about", about=er)

  @app.get("/api/about")
  async def api_about(request):
    import pymergetic.metal.authors as authors

    _ = request
    about = authors.about()
    if not isinstance(about, dict):
      return json_response({"error": "unavailable"}, 503)
    return json_response(about)
