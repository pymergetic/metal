"""
``metal.aio`` — sync clocks + Metal awaitables.

Runtime: C module in ``py_aio_mod.c``.
``await`` parks the Python task on a ``pm_metal_async_handle_t``.

Note: named ``aio`` not ``async`` — ``async`` is a Python keyword, so
``import metal.async`` / ``metal.async.sleep_us`` are SyntaxErrors.
"""

from typing import Any, Generator

class MetalAwaitable:
	"""Iterator-style awaitable wrapping a Metal async handle."""

	def __iter__(self) -> Generator[Any, Any, Any]: ...
	def __next__(self) -> Any: ...
	def __await__(self) -> Generator[Any, Any, Any]: ...

def mono_us() -> int:
	"""Monotonic microseconds (sync)."""
	...

def sleep_us(us: int) -> MetalAwaitable:
	"""Async sleep; returns a Metal awaitable (use with ``await``)."""
	...

def yield_() -> MetalAwaitable:
	"""Fairness yield; returns a Metal awaitable (use with ``await``)."""
	...
