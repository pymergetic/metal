"""Unified browse-UI render for the on-device seat (runtime, live registry).

The metal seat is a CDN of its own stuff: it renders the *same* utemplate
sources the CDN uses (home.html / package.html / shell.html, synced from
wasmmod-cdn into www/) but shapes the context from the seat's own live wasmmod
registry instead of the CDN database. That is the one-UI-source contract —
templates are identical; only the data driver differs.

Mapping: "Browse" lists the seat's loaded modules; "a package" is one module
FQN; its artifact is the registered card (`<fqn>.card`, live exports); and the
package page's Inspect viewer is the same dual-pane commander + hljs the CDN
serves (vendored under www/static), backed by the CDN-shaped /artifacts and
/packages API in ~artifacts. The templates read plain attr-addressable dicts
(utemplate emits ``d.field``), so nested dicts are wrapped in an
attribute-addressable class; strings are HTML-escaped where the CDN does, and
hrefs are seat-local (inspect + packs + artifacts).
"""

try:
    import json
except ImportError:  # pragma: no cover - MicroPython
    import ujson as json

_vtstate = {}
_TPLDIR = "www"  # overridden by the shell; relative dir holding *.html


def _vts():
    """Return the vendored utemplate compiler, importing it on first use.

    Kept lazy (not imported at module scope) so importing :mod:`catalog_render`
    never runs the nested ``from pymergetic.metal.net.microdot._utemplate
    import ...``: that path walks the registry namespace machinery, which can
    fault natively when a registry namespace object sits at an ancestor of the
    import name. The seat renders with :class:`FrozenEngine` and never needs the
    on-disk compiler, so loading this module must not touch it.
    """
    cached = _vtstate.get("v")
    if cached is not None:
        return cached
    if _vtstate.get("done"):
        return None
    try:
        from pymergetic.metal.net.microdot._utemplate import source as src

        _vtstate["v"] = src
        _vtstate["err"] = None
    except Exception as _e:  # host tooling without the seat package
        # Keep the cause: importing the vendored compiler runs microdot's
        # __init__, so an unrelated shadowed stdlib surfaces here and
        # "utemplate not available" alone sends you looking in the wrong place.
        _vtstate["v"] = None
        _vtstate["err"] = repr(_e)
    _vtstate["done"] = True
    return _vtstate["v"]


def _vterr():
    return _vtstate.get("err")


def configure(template_dir):  # pragma: no cover - shell owns the real path
    global _TPLDIR
    _TPLDIR = template_dir


def _esc(v):
    s = "" if v is None else str(v)
    return (
        s.replace("&", "&amp;")
        .replace('"', "&quot;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


class Attr(dict):
    """dict with attribute access, so utemplate's ``d.field`` resolves."""

    def __getattr__(self, item):
        try:
            return self[item]
        except KeyError:
            raise AttributeError(item) from None


def _attr(v):
    if isinstance(v, dict):
        return Attr(dict((k, _attr(x)) for k, x in v.items()))
    if isinstance(v, (list, tuple)):
        return [_attr(x) for x in v]
    return v


def _modules(registry):
    """Normalize a seat registry's ``modules`` face to a concrete list of FQNs.

    The slot can be a list or a callable returning a list. Both become a plain
    ``list[str]`` so ``sorted()``/``in`` always see a typed iterable instead of
    the untyped ``object`` a dict index yields.
    """
    mods = registry.get("modules") or []
    if callable(mods):
        got = mods()
        if isinstance(got, (list, tuple)):
            items = list(got)
        else:
            items = []
    elif isinstance(mods, (list, tuple)):
        items = list(mods)
    else:
        items = []
    return [str(fqn) for fqn in items]


def _cards(fqns, export_count=None):
    """Of the module FQNs, keep only the seat's *cards* (system module tree).

    The registry's ``modules`` face lists every module ever imported or
    presence-published — including the interpreter's own Python modules
    (``json``, ``_thread``, the frozen renderer ``catalog_render``/
    ``metal_packs``/``package_html``, ``asyncio``). None of those is a browsable
    package. A card lives under a seat package namespace — this seat's own
    ``pymergetic.metal.*`` cards (build-time ledger or runtime registry) and the
    system ``pymergetic.wasmmod.*`` / guest-pack namespaces it can load. A
    namespace rule (not an export-count rule) is what correctly keeps e.g. the
    real ``pymergetic.metal.async`` card even when it registers zero C/Rust
    exports, while still dropping interpreter/Python-implementation modules that
    never live under ``pymergetic.*``.
    """
    keep = []
    for fqn in fqns:
        if fqn.startswith("pymergetic."):
            keep.append(fqn)
    return keep


def _href(*parts):
    out = "/".join(p.strip("/") for p in parts if p and p.strip("/"))
    return "/" + out if out else "/"


def _fmt(n):
    return str(n)


def _role_of(fqn):
    if fqn.startswith("pymergetic.metal.arch.") or fqn.endswith(".arch"):
        return "arch"
    if fqn.startswith("pymergetic.metal.unix."):
        return "host"
    if fqn.startswith("pymergetic.metal."):
        return "kernel"
    return ""


def role_title(role):
    return {
        "arch": "Architecture image — freestanding; Inspect only",
        "host": "Unix host — Inspect only; Play disabled",
        "kernel": "Kernel module — Inspect only; Play disabled",
    }.get(role, "Module — Inspect; Play disabled")


def _meta(modules, export_count):
    """Shape a catalog row list from the live registry module ledger.

    ``modules`` must be callable returning a list of FQN strings and
    ``export_count`` a callable fqn -> int (same faces :mod:`api` uses).
    """
    out = []
    for fqn in sorted(modules() or []):
        exports = int(export_count(fqn))
        out.append({
            "name": fqn,
            "name_lower": fqn.lower(),
            "channel": "lead",
            "version": _fmt(exports),
            "description": "%s exports" % exports if exports else "",
            "artifact_count": exports,
            "version_count": 1,
            "deps": {},
            "deps_ok": {},
            "needed_by": [],
            "yanked": False,
            "deprecated": False,
            "updated_at": None,
            "origin": "local",
            "role": _role_of(fqn),
            # The rendered page, not the JSON API: /inspect/reg/<fqn> is the
            # machine face and a browser just shows the raw object.
            "href": _href("packs", fqn),
            "data_search": fqn.lower(),
            "data_yanked": "0",
            "data_deprecated": "0",
            "data_updated": "",
            "updated_str": "",
            "deps_len": 0,
            "needed_len": 0,
            "lead_pill": " pill-lead",
            "dep_links": [],
            "needed_links": [],
        })
    return out


def _nav(tree_roots):
    """Build the sidebar tree HTML (recursive — Python, not template)."""
    return _nav_ul(tree_roots, 0)


def _nav_ul(roots, depth):
    # The root list is the bare tree; every nested <ul> is a foldable child
    # branch and carries `tree-children` — the class the .tree-children CSS
    # (indent + left tree-line) and the shell's setOpen() collapse JS contract
    # on (srcless `:scope > .tree-children`). Emitting anything else makes the
    # subtrees sit flat and uncollapsible.
    cls = "tree tree-roots" if depth == 0 else "tree tree-children"
    parts = ['<ul class="%s">' % cls]
    for r in roots:
        parts.append(_nav_li(r, depth + 1))
    parts.append("</ul>")
    return "".join(parts)


def _nav_li(node, depth):
    children = node.get("children") or []
    is_pkg = bool(node.get("is_package"))
    fqn = node.get("fqn") or node.get("name", "")
    cls = ["tree-node"]
    cls.append("is-folder" if children else "is-leaf")
    if is_pkg:
        cls.append("is-package")
    role = node.get("role")
    if role:
        cls.append("is-" + role)
    data = ' data-name="%s" data-package="%s"' % (_esc(fqn.lower()), _esc(fqn))
    name = _esc(node.get("name", ""))
    maxlen = node.get("_maxlen", 0)
    short = _esc(node.get("name", "")[:maxlen]) if maxlen else name
    out = ['<li class="%s"%s>' % (" ".join(cls), data)]
    # The chevron column is a toggle when the node folds, a spacer otherwise;
    # the label is a package link whenever the node is itself a card. A node
    # can be both, so these two choices are made independently.
    if children:
        lead = ('<button type="button" class="tree-toggle" aria-expanded="false">'
                '<span class="tree-chevron" aria-hidden="true"></span></button>')
    else:
        lead = '<span class="tree-toggle-spacer" aria-hidden="true"></span>'
    if is_pkg:
        label = ('<a class="tree-pkg tree-name" href="%s" data-package="%s">%s</a>'
                 % (_href("packs", fqn), _esc(fqn), short))
    else:
        label = '<span class="tree-name">%s</span>' % short
    out.append('<div class="tree-row">%s%s</div>' % (lead, label))
    if children:
        out.append(_nav_ul(children, depth))
    out.append("</li>")
    return "".join(out)


def _tree_of(fqns):
    """Bucket module FQNs into a fold-tree by dotted prefix (like the CDN nav).

    Each node carries both its display segment (``name``) and its full dotted
    path (``fqn``) — the link target is the FQN, since /inspect/reg resolves
    modules by full name, not by leaf segment. A node may be *both* a package
    and a folder (``pymergetic.metal.net.http`` is a card and the parent of
    ``.asgi``), so ``is_package`` and ``children`` are independent.
    """
    fqset = set(fqns)
    root = {}  # name -> node

    def ensure(children, name, dotted):
        n = children.get(name)
        if n is None:
            n = {"name": name, "fqn": dotted, "children": {}}
            children[name] = n
        return n

    for fqn in fqns:
        parts = fqn.split(".")
        cur = root
        acc = []
        for p in parts:
            acc.append(p)
            dotted = ".".join(acc)
            node = ensure(cur, p, dotted)
            if dotted in fqset:
                node["is_package"] = True
                node.setdefault("role", _role_of(dotted))
            cur = node["children"]

    def finish(children):
        out = []
        for name in sorted(children):
            n = children[name]
            n["children"] = finish(n["children"])
            out.append(n)
        return [c for c in out if c["children"] or c.get("is_package")]

    return finish(root)


def shell_ctx(catalog, *, title, active_package="", body_html="", nav_html_override=None, base_path=""):
    d = {
        "title": title,
        "site_css": _href("static", "site.css"),
        "page_head": "",
        "body_class": "",
        "base_path": base_path,
        "experimental": False,
        "experimental_message": "",
        "brand_name": "pymergetic.metal",
        "home_href": _href("inspect", ""),
        "brand_logo": _href("static", "img", "pymergetic.png"),
        "health_href": _href("health"),
        "main_class": "",
        "active_package": active_package,
        "current_user": None,
        "user_email": "",
        "nav_browse": "/",
        "nav_users": "",   # no user accounts on the seat
        "nav_publish": "",  # publishing is a host/CDN activity
        "nav_sessions": "",  # no browser sessions to list on the seat
        "nav_docs": _href("docs"),  # FastAPI-style interactive docs on the seat
        "nav_login": "",
        "nav_browse_cls": "is-active" if (nav_html_override is None or active_package == "") else "",
        "nav_users_cls": "",
        "nav_publish_cls": "",
        "nav_sessions_cls": "",
        "nav_docs_cls": "",
        "nav_login_cls": "",
        "nav_federation": "",
        "nav_federation_cls": "",
        "experimental_repl": False,
        "repl_ready": False,
        "repl_default_engine": "mp",
        "repl_asset_v": "",
        "repl_mjs_url": "",
        "repl_autoexec": "",
        "repl_js_src": "",
        "cdn_base": "",
        "repl_engines": [],
        "content": body_html,
        "content_nav": nav_html_override if nav_html_override is not None else "",
        "app_version": "seat",
        # The shared dual-pane Inspect commander, vendored here (www/static/inspect)
        # exactly as the CDN serves it — not the plain /inspect console.
        "inspect_js": _href("static", "inspect", "main.js"),
    }
    return d


class Engine:
    """Tiny recompile-on-read utemplate Engine over a template directory."""

    def __init__(self, template_dir, compiled_dir=None):
        self.tpl = template_dir
        self.comp = compiled_dir
        self._cache = {}

    def compile(self, name):
        """Emit the compiled template module without rendering it.

        The seat renders from these modules (FrozenEngine), so a template it
        never renders at build time still has to be compiled for it.
        """
        import os
        vts = _vts()
        if vts is None:
            raise RuntimeError("utemplate not available: %s" % (_vterr() or "import failed"))
        comp = self.comp or (self.tpl + "/_compiled")
        os.makedirs(comp, exist_ok=True)
        cpath = os.path.join(comp, name.replace(".", "_") + ".py")
        with open(os.path.join(self.tpl, name)) as fi, open(cpath, "w") as fo:
            vts.Compiler(fi, fo, loader=None).compile()
        return cpath

    def render(self, name, ctx):
        import os
        src = os.path.join(self.tpl, name)
        vts = _vts()
        if vts is None:
            raise RuntimeError("utemplate not available: %s" % (_vterr() or "import failed"))
        mod = self._cache.get(name)
        if mod is None:
            comp = self.comp or (self.tpl + "/_compiled")
            os.makedirs(comp, exist_ok=True)
            cpath = os.path.join(comp, name.replace(".", "_") + ".py")
            with open(src) as fi, open(cpath, "w") as fo:
                c = vts.Compiler(fi, fo, loader=None)
                c.compile()
            import importlib.util
            spec = importlib.util.spec_from_file_location("_ut_%s" % name.replace(".", "_"), cpath)
            if spec is None:
                raise RuntimeError("compiled template spec could not be created: %s" % cpath)
            loader = spec.loader
            assert loader is not None, "compiled template spec has no loader: %s" % cpath
            mod = importlib.util.module_from_spec(spec)
            loader.exec_module(mod)
            self._cache[name] = mod
        return "".join(mod.render(_attr(ctx)))


class FrozenEngine:
    """Render templates that were compiled ahead of time — the seat's engine.

    utemplate turns a template into a plain Python module, so the seat imports
    ``package_html`` instead of carrying the compiler, the .html files and a
    writable directory to compile into. Same template source as the CDN, one
    build step earlier; firmware needs no filesystem for it.
    """

    def __init__(self):
        self._cache = {}

    def render(self, name, ctx):
        mod = self._cache.get(name)
        if mod is None:
            mod = __import__(name.replace(".", "_"))
            self._cache[name] = mod
        return "".join(mod.render(_attr(ctx)))


def render_home(registry, *, engine, template_dir="www", active_package=""):
    """Render the unified browse view (home.html in shell.html) from a live registry."""
    fqns = _cards(_modules(registry))
    exp = registry.get("export_count") or (lambda fqn: 0)
    mods_loader = lambda: fqns
    catalog = _meta(mods_loader, exp)
    roots = _tree_of(sorted(fqns))
    for r in roots:  # cap deep names in nav for legibility
        r["_maxlen"] = 0
    nav = _nav(roots)
    body = engine.render("home.html", {"catalog": catalog, "catalog_len": len(catalog)})
    shell = shell_ctx(catalog, title="pymergetic.metal — seat", body_html=body, nav_html_override=nav)
    return engine.render("shell.html", shell)


def render_package(registry, name, *, engine, template_dir="www"):
    """Render one module as a CDN-style package page (package.html in shell.html)."""
    fqns = _cards(_modules(registry))
    if name not in fqns:
        return None
    exp = registry.get("export_count") or (lambda fqn: 0)
    exports = int(exp(name))
    entry = {
        "version": _fmt(exports),
        "aot_version": None,
        "deps": {},
        "artifacts": [{
            # The seat's own module. There is no published .wasm/.elf file to
            # download — the cargo is the live registry card, a set of registered
            # C/Rust exports. Its artifact root is a real, commander-driven card
            # (`/artifacts/lead/<fqn>.card/...` answers inspect/sections/symbols
            # from the live registry), so the row pairs with the same dual-pane
            # Inspect commander + hljs the CDN serves. No fabrication: the path
            # names the live card, and the inspect payload is the honest export
            # list from ~artifacts.
            "path": name + ".card",
            "kind": "card",
            "encoding": "registry",
            "size": 0,
            "arch": None,
            "version_arg": "",
            "size_str": "%s exports" % exports,
            "dl_href": _href("artifacts", "lead", name + ".card"),
            "inspect_href": _href("artifacts", "lead", name + ".card", "inspect"),
            "files_base": _href("artifacts", "lead", name + ".card", "files"),
            "files_raw": _href("artifacts", "lead", name + ".card", "files", "raw"),
            "sections_raw": _href("artifacts", "lead", name + ".card", "sections", "raw"),
            "encoding_raw": False,
            "ends_elf": False,
            "ends_efi": False,
            "ends_efi_zlib": False,
            "ends_elf_zlib": False,
            "is_wasm": False,
        }],
        "maintainer_email": "seat@inspect",
        "description": "%s registered exports" % exports,
        "homepage": None,
        "license": None,
        "yanked": False,
        "yank_reason": None,
        "deprecated": False,
        "successor": None,
        "contents": None,
    }
    role = _role_of(name)
    ctx = {
        "name": name,
        "channel": "lead",
        "package_role": role or "module",
        "cdn_base": "",
        "is_unix_seat": role == "host",
        "is_arch_seat": role == "arch",
        "fed_origin": "local",
        "fed_peer_label": "",
        "fed_peer_browse_url": "",
        "needed_by": [],
        "deps_ok": {},
        "package_versions": [],
        "entry": entry,
        "version_arg": "",
        "experimental_repl": False,
        "repl_ready": False,
        "package_role": role or "module",
        "active_package": name,
        "package_versions_len": 0,
        "version_fold_count": 0,
        "artifacts_len": 1,
        "has_elf": False,
        "has_efi": False,
        "no_play_role": True,
        "show_try": False,
        "channel_href": _href("packs", name),
        "author_href": "",
        "version_arg": name.split(".")[-1],
        "deps_links": [],
        "needed_links": [],
        "package_versions": [],
    }
    # The package template lists the same vendored hljs lang scripts the CDN
    # ships. They are served from the seat's own www/static/vendor (embedded
    # bytes), so syntax highlighting here is the real thing — not the plain
    # inspect console JS pretending to be a highlighter.
    _VENDOR = [
        "highlight.min.js", "lang-python.min.js", "lang-c.min.js", "lang-cpp.min.js",
        "lang-rust.min.js", "lang-javascript.min.js", "lang-typescript.min.js",
        "lang-json.min.js", "lang-markdown.min.js", "lang-ini.min.js", "lang-yaml.min.js",
        "lang-xml.min.js", "lang-bash.min.js", "lang-makefile.min.js", "lang-cmake.min.js",
        "lang-diff.min.js", "lang-llvm.min.js", "lang-go.min.js", "lang-java.min.js",
        "lang-sql.min.js", "lang-ruby.min.js", "lang-perl.min.js", "lang-dockerfile.min.js",
        "lang-lisp.min.js", "lang-x86asm.min.js",
    ]
    for i, v in enumerate(_VENDOR):
        ctx["vendor_%d" % i] = _href("static", "vendor", v)
    body = engine.render("package.html", ctx)
    nav = _nav(_tree_of(fqns))
    shell = shell_ctx([], title="%s — pymergetic.metal" % name, body_html=body,
                      nav_html_override=nav, active_package=name)
    return engine.render("shell.html", shell)
