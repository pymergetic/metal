"""Lang pool: one impl in, emit the other pool faces.

Pool slots (codegen):
  c     — C ABI face ``{base}.h`` (covers impl=c and impl=cpp)
  rs    — Rust face ``{base}.rs``
  py    — Python stubs ``{base}.pyi``
  toml  — optional dump ``{base}.toml`` (output-only; never an impl)

Default sync emits every *default* slot except the impl's slot.
``toml`` is not default — opt in with ``--emit toml``.
"""
from __future__ import annotations

# .module impl= values (human source language)
IMPL_EXT = {
    "rs": "rs",
    "c": "c",
    "cpp": "cpp",
    "py": "py",
}

# Codegen pool slots
POOL = ("c", "rs", "py", "toml")

# Written on a normal sync (not toml)
DEFAULT_EMIT = frozenset({"c", "rs", "py"})

# Slot metadata: can_import = may be .module impl; face = relative output name
_SLOT = {
    "c": {"can_import": True, "can_emit": True, "face": "{base}.h"},
    "rs": {"can_import": True, "can_emit": True, "face": "{base}.rs"},
    "py": {"can_import": True, "can_emit": True, "face": "{base}.pyi"},
    "toml": {"can_import": False, "can_emit": True, "face": "{base}.toml"},
}


def pool_slot(impl: str) -> str:
    """Map .module impl to pool slot. cpp lives under c."""
    if impl in ("c", "cpp"):
        return "c"
    if impl not in IMPL_EXT and impl != "toml":
        raise ValueError(f"unknown impl {impl!r}")
    return impl


def face_rel(slot: str, base: str) -> str:
    meta = _SLOT[slot]
    return meta["face"].format(base=base)


def emit_slots(impl: str, extra: frozenset[str] | set[str] | None = None) -> list[str]:
    """Pool slots to emit for this impl (never the impl's own slot)."""
    own = pool_slot(impl)
    wanted: set[str] = set(DEFAULT_EMIT)
    if extra:
        for s in extra:
            if s not in _SLOT:
                raise ValueError(f"unknown emit slot {s!r} (pool={list(POOL)})")
            if not _SLOT[s]["can_emit"]:
                raise ValueError(f"slot {s!r} cannot be emitted")
            wanted.add(s)
    wanted.discard(own)
    # stable order matching POOL
    return [s for s in POOL if s in wanted]


def all_face_rels(base: str) -> list[str]:
    """Every possible generated face path for this module stem."""
    return [face_rel(s, base) for s in POOL]
