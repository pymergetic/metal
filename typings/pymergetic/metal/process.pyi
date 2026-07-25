"""
``pymergetic.metal.process`` — Python view of the live-process table.

Runtime: C module in ``guest/process/process_py_bind.c``, wired via PM_METAL_PY_BIND.
Lets a script observe completion of a FACADE-shaped command started via
``pmcmd.<name>(...)`` with a plain polling loop (no new host logic).
"""

from typing import TypedDict

class ProcessInfo(TypedDict):
	id: int
	name: str
	state: int
	ui_kind: int
	tab: int
	surface: int

def poll() -> tuple[int, int]:
	"""Pump the current live process. Returns (rc, status):
	rc: 1 done ok, -1 error, 0 still running / none.
	"""
	...

def active() -> int:
	"""1 if a process is currently live, else 0."""
	...

def current() -> int:
	"""Current live process id (0 if none)."""
	...

def info(id: int) -> ProcessInfo | None:
	"""Snapshot of a process slot, or None if id is unknown."""
	...
