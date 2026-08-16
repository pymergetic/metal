"""Honest GET /inspect/self body (no forged source trees)."""


def self_description(host_role, theme, **extra):
    """Compact PackageContents-like self-desc.

    Guest metal → kernel identity (pymergetic.metal).
    CDN → host engine identity (pymergetic.wasmmod) until inventory fill-in.
    has_source/has_pack stay false until a real embed-host or published artifact.
    """
    if host_role == "cdn":
        name = "pymergetic.wasmmod"
        role = "engine"
        product = "wasmmod"
        static_backend = "none"
    else:
        name = "pymergetic.metal"
        role = "kernel"
        product = "metal"
        static_backend = "wasmmod"
    body = {
        "schema": 1,
        "name": name,
        "role": role,
        "product": product,
        "org": "pymergetic",
        "theme": theme,
        "has_source": False,
        "has_pack": host_role != "cdn",
        "static_backend": static_backend,
        "source_files": [],
        "pack_files": (["httpd.json"] if host_role != "cdn" else []),
        "tags": {"role": role, "product": product, "org": "pymergetic"},
    }
    body.update(extra)
    return body
