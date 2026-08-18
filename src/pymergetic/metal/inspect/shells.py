"""Inspect shells: one handler core, two thin app mounts.

`make_shell(role)` picks the shell — MicrodotShell on-device (metal), FastAPIShell
on the CDN (cdn). Both mount the identical `api.py` `(json, status)` handlers; only
the web framework differs. Swap the shell, the handlers stay the same.
"""

from .adapter_fastapi import FastAPIShell
from .adapter_microdot import MicrodotShell

__all__ = ["FastAPIShell", "MicrodotShell", "make_shell"]


def make_shell(role="metal", theme="metal", **kw):
    if role == "cdn":
        return FastAPIShell(role=role, theme=theme, **kw)
    return MicrodotShell(role=role, theme=theme, **kw)
