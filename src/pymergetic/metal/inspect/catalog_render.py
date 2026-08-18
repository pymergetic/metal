"""Unified browse-UI render for the on-device seat (runtime, live registry).

The metal seat is a CDN of its own stuff: it renders the *same* utemplate
sources the CDN uses (home.html / package.html / shell.html, synced from
wasmmod-cdn into www/) but shapes the context from the seat's own live wasmmod
registry instead of the CDN database. That is the one-UI-source contract —
templates are identical; only the data driver differs.

Mapping: "Browse" lists the seat's loaded modules; "a package" is one module
FQN; its artifacts are the registered exports (export count / signature); and
"Inspect" stays the live registry/RPC console at /inspect/. The templates read
plain attr-addressable dicts (utemplate emits ``d.field``), so nested dicts are
wrapped in an attribute-addressable class; strings are HTML-escaped where the
CDN does, and hrefs are seat-local (inspect + packs).
"""

from __future__ import annotations

try:
    import json
except ImportError:  # pragma: no cover - MicroPython
    import ujson as json

try:
    from pymergetic.metal.net.microdot._utemplate import source as _vtsource
except Exception:  # pragma: no cover - host tooling without the seat package
    _vtsource = None

_TPLDIR = "www"  # overridden by the shell; relative dir holding *.html


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
            "href": _href("inspect", "reg", fqn),
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
    parts = ['<ul class="tree tree-roots">']
    for r in roots:
        parts.append(_nav_li(r, depth + 1))
    parts.append("</ul>")
    return "".join(parts)


def _nav_li(node, depth):
    cls = ["tree-node"]
    if node.get("children"):
        cls.append("is-folder" if not node.get("is_package") else "is-leaf")
    else:
        cls.append("is-leaf")
    if node.get("is_package"):
        cls.append("is-package")
    role = node.get("role")
    if role:
        cls.append("is-" + role)
    data = ' data-name="%s" data-package="%s"' % (_esc(node.get("name", "").lower()), _esc(node.get("name", "")))
    name = _esc(node.get("name", ""))
    href = _href("inspect", "reg", node.get("name", ""))
    maxlen = node.get("_maxlen", 0)
    short = _esc(node.get("name", "")[:maxlen]) if maxlen else name
    out = ['<li class="%s"%s>' % (" ".join(cls), data)]
    if node.get("children") and not node.get("is_package"):
        out.append('<div class="tree-row"><button type="button" class="tree-toggle" aria-expanded="false">'
                   '<span class="tree-chevron" aria-hidden="true"></span></button>'
                   '<span class="tree-name">%s</span></div>' % short)
        out.append(_nav_ul(node["children"], depth))
    else:
        out.append('<div class="tree-row"><span class="tree-toggle-spacer" aria-hidden="true"></span>'
                   '<a class="tree-pkg tree-name" href="%s" data-package="%s">%s</a></div>'
                   % (href, _esc(node.get("name", "")), short))
    out.append("</li>")
    return "".join(out)


def _tree_of(fqns):
    """Bucket module FQNs into a fold-tree by dotted prefix (like the CDN nav).

    A node whose dotted name is exactly a loaded module FQN renders as a
    package leaf (links to its package page); anything else is a folder.
    """
    fqset = set(fqns)
    root = {}  # name -> node

    def ensure(children, name):
        n = children.get(name)
        if n is None:
            n = {"name": name, "children": {}}
            children[name] = n
        return n

    for fqn in fqns:
        parts = fqn.split(".")
        cur = root
        acc = []
        for p in parts:
            acc.append(p)
            node = ensure(cur, p)
            dotted = ".".join(acc)
            if dotted in fqset:
                node["is_package"] = True
                node.setdefault("role", _role_of(dotted))
            cur = node["children"]

    def finish(children):
        out = []
        for n in children.values():
            n["children"] = finish(n["children"])
            n["children"] = [c for c in n["children"] if c["children"] or c.get("is_package")]
            out.append(n)
        return out

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
        "brand_logo": "",
        "health_href": _href("health"),
        "main_class": "",
        "active_package": active_package,
        "current_user": None,
        "user_email": "",
        "nav_browse": "/",
        "nav_users": "",   # no user accounts on the seat
        "nav_publish": "",  # publishing is a host/CDN activity
        "nav_sessions": "",  # no browser sessions to list on the seat
        "nav_docs": _href("inspect", ""),  # the live RPC console is the seat's docs
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
        "inspect_js": _href("inspect", "js", "inspect.js"),
    }
    return d


class Engine:
    """Tiny recompile-on-read utemplate Engine over a template directory."""

    def __init__(self, template_dir, compiled_dir=None):
        self.tpl = template_dir
        self.comp = compiled_dir
        self._cache = {}

    def render(self, name, ctx):
        import os
        src = os.path.join(self.tpl, name)
        if _vtsource is None:
            raise RuntimeError("utemplate not available")
        mod = self._cache.get(name)
        if mod is None:
            comp = self.comp or (self.tpl + "/_compiled")
            os.makedirs(comp, exist_ok=True)
            cpath = os.path.join(comp, name.replace(".", "_") + ".py")
            with open(src) as fi, open(cpath, "w") as fo:
                c = _vtsource.Compiler(fi, fo, loader=None)
                c.compile()
            import importlib.util
            spec = importlib.util.spec_from_file_location("_ut_%s" % name.replace(".", "_"), cpath)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            self._cache[name] = mod
        return "".join(mod.render(_attr(ctx)))


def render_home(registry, *, engine, template_dir="www", active_package=""):
    """Render the unified browse view (home.html in shell.html) from a live registry."""
    mods = registry["modules"]
    mods_loader = mods if callable(mods) else (lambda: mods)
    exp = registry.get("export_count") or (lambda fqn: 0)
    catalog = _meta(mods_loader, exp)
    roots = _tree_of(sorted(mods_loader() or []))
    for r in roots:  # cap deep names in nav for legibility
        r["_maxlen"] = 0
    nav = _nav(roots)
    body = engine.render("home.html", {"catalog": catalog, "catalog_len": len(catalog)})
    shell = shell_ctx(catalog, title="pymergetic.metal — seat", body_html=body, nav_html_override=nav)
    return engine.render("shell.html", shell)


def render_package(registry, name, *, engine, template_dir="www"):
    """Render one module as a CDN-style package page (package.html in shell.html)."""
    mods = registry["modules"]
    mods_loader = mods if callable(mods) else (lambda: mods)
    fqns = list(mods_loader() or [])
    if name not in fqns:
        return None
    exp = registry.get("export_count") or (lambda fqn: 0)
    exports = int(exp(name))
    entry = {
        "version": _fmt(exports),
        "aot_version": None,
        "deps": {},
        "artifacts": [{
            "path": name.replace(".", "/") + ".wasm",
            "kind": "wasm",
            "encoding": "raw",
            "size": 0,
            "arch": None,
            "version_arg": "",
            "size_str": "%s exports" % exports,
            "dl_href": _href("inspect", "reg", name),
            "inspect_href": _href("inspect", "reg", name),
            "files_base": _href("inspect", "reg", name),
            "files_raw": _href("inspect", "reg", name),
            "sections_raw": _href("inspect", "reg", name),
            "encoding_raw": True,
            "ends_elf": False,
            "ends_efi": False,
            "ends_efi_zlib": False,
            "ends_elf_zlib": False,
            "is_wasm": True,
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
    for i in range(25):
        # The package template lists the CDN's hljs vendored lang scripts; on the
        # seat there is no source-tree highlighter, so point them at the bundled
        # inspect console JS (harmless; source highlighting is CDN-only).
        ctx["vendor_%d" % i] = _href("inspect", "js", "inspect.js")
    body = engine.render("package.html", ctx)
    nav = _nav(_tree_of(fqns))
    shell = shell_ctx([], title="%s — pymergetic.metal" % name, body_html=body,
                      nav_html_override=nav, active_package=name)
    return engine.render("shell.html", shell)
