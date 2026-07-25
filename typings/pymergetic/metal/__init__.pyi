"""Firmware-builtin ``pymergetic.metal`` package (docs/MICROPYTHON.md)."""

from . import aio as aio
from . import mod as mod
from . import process as process

__path__: list[str]
__all__ = ["aio", "mod", "process"]
