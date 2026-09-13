"""Server-rendered package pages for the seat: the deferred-route renderer.

The asgi card cannot render these itself — the templates are Python (utemplate).
asgi parks a request on a deferred route and this module drains the queue and
answers. The renderer is a vm_only coroutine handed to the async runtime
(metal.register_upy), so any async-runner core may step it — each runner core
that steps a vm_only task has its own MicroPython thread state and re-enters the
interpreter under the VM lock. No raw Python thread, no one-slot VM monopoly:

    metal_packs.start() # route_defer + register_upy(vm-only render pump)
    metal_packs.pump()  # one drain cycle, for a loop that calls itself in
    metal_packs.serve_forever()  # call-only install (blocks, like app.run())

A page renders from the live registry through the same templates the CDN serves,
so /packs/<fqn> is the CDN's package view of this seat's own module.

Frozen under a top-level name on purpose: `pymergetic.metal.*` is a card
namespace, and a card module has no __path__ for a Python submodule to hang off.
"""

try:
    import catalog_render as _cr
except ImportError:  # host tooling imports it as the package submodule
    from pymergetic.metal.inspect import catalog_render as _cr

_PREFIX = "/packs/"
_engine = None


def _openapi():
    try:
        import openapi as m
    except ImportError:  # host tooling imports it as the package submodule
        from pymergetic.metal.inspect import openapi as m
    return m


def _artifacts():
    try:
        import artifacts as m
    except ImportError:  # host tooling imports it as the package submodule
        from pymergetic.metal.inspect import artifacts as m
    return m


def _repl():
    try:
        import metal_repl as m
    except ImportError:  # host tooling imports it as the package submodule
        from pymergetic.metal.inspect import metal_repl as m
    return m


def _asgi():
    import pymergetic.metal.net.http.asgi as asgi

    return asgi

def _registry():
    """The seat's own registry, shaped the way catalog_render wants it."""
    import pymergetic.wasmmod as w

    exp = getattr(w, "export_count", None)
    return {
        "modules": lambda: sorted(w.modules()),
        "export_count": (lambda fqn: int(exp(fqn))) if exp else (lambda fqn: 0),
    }


_JSON_ROUTES = (
    ("/artifacts/*", "application/json"),
    ("/packages", "application/json"),
    ("/packages/*", "application/json"),
)

# The console panel's write half. It parks like a page does, but it runs a
# line rather than rendering one, so it answers POST — asking for a page
# twice is harmless and running a command twice is not. The read half is
# GET /console/<id>?since= in the inspect card, which needs no interpreter.
_EXEC_ROUTE = "/console/exec"


def engine():
    global _engine
    if _engine is None:
        _engine = _cr.FrozenEngine()
    return _engine


def install(pattern=None):
    """Register the page + introspect + docs routes. Returns 0 on success."""
    asgi = _asgi()
    if int(asgi.route_defer(pattern or (_PREFIX + "*"), "text/html; charset=utf-8")) != 0:
        return -1
    # The CDN-shaped introspect API (/artifacts/...) and package catalog nav
    # (/packages, /packages/<name>/versions) that the shared Inspect commander
    # fetches, plus the FastAPI-style docs page. One pump drains them all —
    # defer_reply answers each with the route's own registered content-type.
    for path, ctype in _JSON_ROUTES:
        if int(asgi.route_defer(path, ctype)) != 0:
            return -1
    if int(asgi.route_defer_m("POST", _EXEC_ROUTE, "application/json")) != 0:
        return -1
    if _openapi().install_openapi_deferred(asgi) != 0:
        return -1
    return 0


def _console_seq():
    """Where console 0 stood before a line ran, so the panel can tell which
    output was that line's. Zero on a seat without the console card."""
    try:
        import pymergetic.metal.console as c

        return int(c.seq())
    except Exception:
        return 0


def render(path):
    """Render the response for a request path. Returns (body_bytes, ctype).

    `ctype` is None to reply with the deferred route's declared type, or a
    NUL-terminated str when the reply should override it (a raw source file's
    own Content-Type).
    """
    if path.startswith(_EXEC_ROUTE):
        # The whole line is one query argument, so the split is on the first
        # '=' only: a line with its own '?' or '&' in it must survive.
        q = path[len(_EXEC_ROUTE):]
        line = ""
        if q.startswith("?cmd="):
            line = _repl().unquote(q[5:])
        seq_before = _console_seq()
        rc = _repl().run(line)
        return (b'{"rc":%d,"since":%d}' % (rc, seq_before)), None
    if path.startswith(_PREFIX):
        fqn = path[len(_PREFIX):]
        if fqn.endswith("/"):
            fqn = fqn[:-1]
        if fqn == "docs" or fqn == "docs/":
            # /packs/docs — API docs rendered through the shared shell chrome.
            # openapi.docs_inner_html() is the swagger bloc; shell wraps it.
            body_html = _openapi().docs_inner_html().decode("utf-8", errors="replace")
            fqns = _cr._cards(_cr._modules(_registry()))
            nav = _cr._nav(_cr._tree_of(sorted(fqns)))
            shell = _cr.shell_ctx([], title="API — pymergetic.metal",
                                  body_html=body_html, nav_html_override=nav,
                                  nav_active="docs",
                                  page_head='<link rel="stylesheet" href="/static/css/swagger-theme.css" />'
                                            '<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui.css" crossorigin>',
                                  page_js=None)
            # The API docs page loads swagger-ui bundle, not the Inspect commander.
            shell["inspect_js"] = ""
            html = _cr.FrozenEngine().render("shell.html", shell)
            return html.encode(), None
        html = None
        if fqn:
            html = _cr.render_package(_registry(), fqn, engine=engine())
        if html is None:
            # An unknown module is a 200 with an honest page, not a blank body: the
            # nav links are generated, so a miss here means the registry moved.
            html = "<!doctype html><title>no such module</title><h1>%s</h1>" % (
                _cr._esc(fqn) or "no module named"
            )
        return html.encode(), None
    # The shared commander's API: artifact + package-catalog routes and the
    # FastAPI-style docs page. One pump answers all of them.
    if path == "/docs" or path == "/docs/":
        # Render through the shared shell chrome when the seat has the renderer.
        body_html = _openapi().docs_inner_html().decode("utf-8", errors="replace")
        fqns = _cr._cards(_cr._modules(_registry()))
        nav = _cr._nav(_cr._tree_of(sorted(fqns)))
        shell = _cr.shell_ctx([], title="API — pymergetic.metal",
                              body_html=body_html, nav_html_override=nav,
                              nav_active="docs",
                              page_head='<link rel="stylesheet" href="/static/css/swagger-theme.css" />'
                                        '<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui.css" crossorigin>',
                              page_js=None)
        shell["inspect_js"] = ""
        html = _cr.FrozenEngine().render("shell.html", shell)
        return html.encode(), None
    body, _status = _openapi().route(path)
    if body is not None:
        return body, None
    body, _status, _binary = _artifacts().route(path)
    ctype = _artifacts().content_type(path) if _binary else None
    return body, ctype


try:
    from sys import print_exception  # type: ignore[attr-defined]
except ImportError:
    import traceback

    def print_exception(exc):
        traceback.print_exc()


def _print_tb(e):
    try:
        print_exception(e)
    except Exception:
        pass


def _render_one(path):
    """Render one request, never letting a failure kill the drain loop.

    The renderer fans in every deferred request on the boot thread; a single
    exception must not end that drainer, or every later request parks forever
    (first package renders, then the rest hang). Log the exception and answer
    an honest error page so the parked connection is released and the queue
    keeps moving.
    """
    try:
        return render(path)
    except Exception as e:  # a page bug must not take down the whole renderer
        _print_tb(e)
        if path.startswith(_PREFIX):
            fqn = path[len(_PREFIX):]
            return ("<!doctype html><title>render error</title>"
                    "<h1>render failed for %s</h1>" % _cr._esc(fqn)).encode(), None
        return _artifacts()._json({"detail": "render error", "path": path,
                                   "error": str(e)}).encode(), None


def pump(budget=4):
    """Answer up to `budget` queued requests. Returns how many were served."""
    asgi = _asgi()
    served = 0
    while served < budget:
        path = asgi.defer_next()
        if path is None:
            break
        body, ctype = _render_one(path)
        try:
            if ctype:
                asgi.defer_reply_ct(body, ctype)
            else:
                asgi.defer_reply(body)
        except Exception as e:  # a bad reply must not end the drain cycle
            _print_tb(e)
        served += 1
    return served


def _pump_loop():
    """Infinite drainer for the asynchronous runtime (metal.register_upy).

    A vm_only coroutine: any runner core may step it, re-entering the interpreter
    under the VM lock. Kept alive no matter what — a stray exception would end the
    generator, and a finished generator is a renderer that stopped draining (every
    later request hangs).
    """
    while True:
        try:
            pump()
        except Exception as e:  # a pump fault must not strand later requests
            _print_tb(e)
        yield


def start(pattern=None):
    """Install the route and get pages rendering. Returns a one-line status.

    The status leads with `ok` or `FAIL` because the seat prints it as the
    detail of the boot tree's `packs` node, where those are the tokens the
    tree paints green and red.

    The renderer is a vm_only coroutine handed to the async runtime: any
    async-runner core may step it under the VM lock. Seats that serve from a bare
    REPL additionally drive the async ring each loop so the pump advances.
    """
    if install(pattern) != 0:
        return "FAIL  route not registered"
    try:
        import pymergetic.metal as m
    except ImportError:
        return "ok  ready (metal_packs.pump()/serve_forever() renders)"
    try:
        m.register_upy(_pump_loop())
    except Exception as e:  # a seat without the vm-only runtime keeps its loop face
        _print_tb(e)
        return "ok  ready (metal_packs.serve_forever() renders)"
    return "ok  rendering"


def serve_forever(idle_ms=5):
    """Drain forever, sleeping when idle. Blocks, the way app.run() does.

    A helper for scripted seats that drive the drain themselves from the boot
    thread, not the interactive REPL surface (which advances the ring each loop).
    The loop is the single drainer every deferred request fans into.
    """
    import time

    sleep = getattr(time, "sleep_ms", None)
    while True:
        try:
            n = pump()
        except Exception as e:
            _print_tb(e)
            n = 0
        if n == 0:
            if sleep:
                sleep(idle_ms)
            else:
                time.sleep(idle_ms / 1000.0)
