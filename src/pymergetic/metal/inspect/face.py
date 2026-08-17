"""Thin Python face — muscle is inspect/__impl__.c."""


def handle(method, path, role="metal", theme="metal"):
    import pymergetic.metal.inspect as m

    _ = role, theme
    st = m.handle(method, path)
    body = m.body()
    if body is None:
        body = ""
    return st, body


def self_description(host_role="metal", theme="metal"):
    _st, body = handle("GET", "/inspect/self", host_role, theme)
    return body
