"""Honest v1 GET /inspect/self body (identity only; no forged source trees)."""


def self_description(host_role, theme, **extra):
    """Compact PackageContents-like self-desc.

    Guest metal → kernel identity (pymergetic.metal).
    CDN → host engine identity (pymergetic.wasmmod) until inventory fill-in.
    has_source/has_pack stay false until a real embed or published artifact.
    """
    if host_role == "cdn":
        name = "pymergetic.wasmmod"
        role = "host"
        product = "wasmmod"
    else:
        name = "pymergetic.metal"
        role = "kernel"
        product = "metal"
    body = {
        "schema": 1,
        "name": name,
        "role": role,
        "product": product,
        "org": "pymergetic",
        "theme": theme,
        "has_source": False,
        "has_pack": False,
        "source_files": [],
        "pack_files": [],
        "tags": {"role": role, "product": product, "org": "pymergetic"},
    }
    body.update(extra)
    return body
