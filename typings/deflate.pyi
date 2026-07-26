"""
``deflate`` — upstream MicroPython extmod (external/micropython/extmod/moddeflate.c),
wired into this port via MICROPY_PY_DEFLATE(+_COMPRESS) in mpconfigport.h.

Hand-authored (not scripts/gen_py_stubs.py output): this is a real upstream
MicroPython builtin, not one of our own pymergetic.metal.* bindings, so it's
never a PM_METAL_PY_BIND call site for the generator to scan. Not in
typeshed either (CPython has no equivalent), hence "could not be resolved"
without this file. Kept in sync by hand with moddeflate.c's locals_dict /
globals_table.
"""

from typing import Any

AUTO: int
RAW: int
ZLIB: int
GZIP: int

class DeflateIO:
    def __init__(
        self, stream: Any, format: int = ..., wbits: int = ..., close: bool = ...
    ) -> None: ...
    def read(self, size: int = ...) -> bytes: ...
    def readinto(self, buf: Any) -> int: ...
    def readline(self) -> bytes: ...
    def write(self, buf: Any) -> int: ...
    def close(self) -> None: ...
    def __enter__(self) -> "DeflateIO": ...
    def __exit__(self, *exc_info: Any) -> None: ...
