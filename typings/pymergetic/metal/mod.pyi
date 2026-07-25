"""
``pymergetic.metal.mod`` — ``mod.<name>.<func>()`` real attribute access
over the mod registry (docs/MODS.md).

Runtime: ``guest/mod/mod_py_bind.c``. ``mod.<name>`` always succeeds (mod names load
on demand); ``<name>.<func>`` raises ``AttributeError`` for an unknown
function (checked via ``pm_metal_mod_func_exists``-equivalent resolve, not
at call time). Calling ``<name>.<func>()`` takes no arguments yet and
returns a Metal awaitable whose ``await`` value is the guest's
``pm_metal_async_set_result_u32()`` payload (0 if none set).

Static analysis can't enumerate real mod/func names ahead of a build, so
this stub only shapes the two proxy levels — see docs/TODO.md for
build-time ``.pyi`` generation.
"""

from typing import Any, Awaitable

class _ModFunc:
	def __call__(self) -> Awaitable[int]: ...

class _ModName:
	def __getattr__(self, name: str) -> _ModFunc: ...

def __getattr__(name: str) -> _ModName: ...
