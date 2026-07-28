# Shared helpers for mods/httpd + mods/api (esc, JSON/HTML, pagination).


def json_response(obj, status=200):
  import json

  return json.dumps(obj), status, {"Content-Type": "application/json"}


def html(tpl, **kwargs):
  from microdot.utemplate import Template

  return Template(tpl).render(**kwargs), 200, {"Content-Type": "text/html"}


def esc(s):
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


def url_query_val(s):
  """Encode a path for ?path= (keep / and .)."""
  if s is None:
    return ""
  out = []
  for c in str(s):
    o = ord(c)
    if (
        (48 <= o <= 57)
        or (65 <= o <= 90)
        or (97 <= o <= 122)
        or c in "-_./~"
    ):
      out.append(c)
    else:
      out.append("%%%02X" % o)
  return "".join(out)


def url_pkg_name(s):
  """Encode an iface pack name for /iface/pkg/<name>.

  Keeps '@' literal (valid in path; microdot <name> is [^/]+ and does not
  percent-decode).
  """
  if s is None:
    return ""
  out = []
  for c in str(s):
    o = ord(c)
    if (
        (48 <= o <= 57)
        or (65 <= o <= 90)
        or (97 <= o <= 122)
        or c in "-_.~@"
    ):
      out.append(c)
    else:
      out.append("%%%02X" % o)
  return "".join(out)


_IFACE_KIND_ORDER = {
    "h": 0,
    "c": 1,
    "pyi": 2,
    "py": 3,
    "meta": 4,
    "sysroot": 5,
}


def iface_pkg_base(name):
  """Split <kind>@<base> -> base; else whole name."""
  if name is None:
    return ""
  s = str(name)
  i = s.find("@")
  if i <= 0:
    return s
  return s[i + 1 :]


def iface_pkg_group_key(name):
  """Module key for /iface tree: <kind>@<base> -> base, strip trailing .docs."""
  base = iface_pkg_base(name)
  if base.endswith(".docs"):
    return base[:-5]
  return base


def iface_pkg_groups(info):
  """Group iface.info() by module, packs sorted by kind then name.

  Returns list of (base, npacks, [{name, href, kind, version, nfiles, blob_len}, ...]).
  Packs named meta@foo.docs nest under foo with the other foo packs.
  """
  if not info:
    return []
  buckets = {}
  for name in info.keys():
    key = iface_pkg_group_key(name)
    meta = info[name]
    kind = meta.get("kind", "") or ""
    buckets.setdefault(key, []).append({
        "name": esc(name),
        "href": "/iface/pkg/" + url_pkg_name(name),
        "kind": esc(kind),
        "version": esc(meta.get("version", "")),
        "nfiles": meta.get("nfiles", 0),
        "blob_len": meta.get("blob_len", 0),
        "_kind_ord": _IFACE_KIND_ORDER.get(kind, 50),
        "_name": name,
    })
  groups = []
  for key in sorted(buckets.keys()):
    packs = buckets[key]
    packs.sort(key=lambda p: (p["_kind_ord"], p["_name"]))
    for p in packs:
      del p["_kind_ord"]
      del p["_name"]
    groups.append((esc(key), len(packs), packs))
  return groups


def iface_norm_path(p):
  if not isinstance(p, str):
    try:
      p = p.decode()
    except Exception:
      p = str(p)
  while p.startswith("./"):
    p = p[2:]
  while p.startswith("/"):
    p = p[1:]
  return p


def int_arg(request, name, default, lo, hi):
  raw = request.args.get(name) if request is not None else None
  if raw is None:
    return default
  try:
    n = int(raw)
  except Exception:
    return default
  if n < lo:
    return lo
  if n > hi:
    return hi
  return n


def qs_href(request, page, limit):
  path = "/"
  if request is not None:
    path = getattr(request, "path", None) or "/"
  parts = []
  if request is not None and request.args is not None:
    for key in ("kind", "module", "name"):
      v = request.args.get(key)
      if v is not None and v != "":
        parts.append(key + "=" + url_query_val(v))
  parts.append("page=" + str(page))
  parts.append("limit=" + str(limit))
  return path + "?" + "&".join(parts)


def paginate(rows, request, default=50, max_limit=500):
  total = len(rows)
  limit = int_arg(request, "limit", default, 1, max_limit)
  pages = (total + limit - 1) // limit if total > 0 else 1
  if pages < 1:
    pages = 1
  page = int_arg(request, "page", 1, 1, pages)
  offset = (page - 1) * limit
  page_rows = rows[offset : offset + limit]
  n = len(page_rows)
  pager = {
      "total": total,
      "page": page,
      "pages": pages,
      "limit": limit,
      "from": (offset + 1) if n else 0,
      "to": offset + n,
      "has_prev": page > 1,
      "has_next": page < pages,
      "prev_href": qs_href(request, page - 1, limit) if page > 1 else "",
      "next_href": qs_href(request, page + 1, limit) if page < pages else "",
      "first_href": qs_href(request, 1, limit),
      "last_href": qs_href(request, pages, limit),
  }
  return page_rows, pager


def json_page(rows, pager):
  return json_response(
      {
          "items": rows,
          "total": pager["total"],
          "page": pager["page"],
          "pages": pager["pages"],
          "limit": pager["limit"],
      }
  )


def doc_row_esc(row):
  return {
      "kind": esc(row.get("kind", "")),
      "key": esc(row.get("key", "")),
      "summary": esc(row.get("summary", "")),
      "sig": esc(row.get("sig", "")),
      "body": esc(row.get("body", "")),
  }


def doc_list_row_esc(row):
  return {
      "kind": esc(row.get("kind", "")),
      "key": esc(row.get("key", "")),
      "summary": esc(row.get("summary", "")),
  }


def sym_row_esc(row):
  return {
      "module": esc(row.get("module", "")),
      "name": esc(row.get("name", "")),
      "sig": esc(row.get("sig", "")),
      "class_": row.get("class_", 0),
      "doc_key": esc(row.get("doc_key", "")),
  }


def about_esc(about):
  authors = []
  for a in about.get("authors") or []:
    authors.append({
        "name": esc(a.get("name", "")),
        "email": esc(a.get("email", "")),
        "role": esc(a.get("role", "")),
    })
  return {
      "version": esc(about.get("version", "")),
      "desc": esc(about.get("desc", "")),
      "url": esc(about.get("url", "")),
      "authors": authors,
  }


def ext_esc(row):
  return {
      "id": esc(row.get("id", "")),
      "version": esc(row.get("version", "")),
      "url": esc(row.get("url", "")),
      "note": esc(row.get("note", "")),
  }


def lim_esc(row):
  return {
      "id": esc(row.get("id", "")),
      "module": esc(row.get("module", "")),
      "name": esc(row.get("name", "")),
      "value": row.get("value", 0),
      "unit": esc(row.get("unit", "")),
      "note": esc(row.get("note", "")),
  }


def metal_version():
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
    if "h@metal.guest" in info and info["h@metal.guest"].get("version"):
      return str(info["h@metal.guest"]["version"])
  except Exception:
    pass
  return "metal"
