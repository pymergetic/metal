"""In-process Inspect dispatch. Routes live in face.py (no relative-import trap)."""

from .face import handle, self_description

__all__ = ["handle", "self_description"]
