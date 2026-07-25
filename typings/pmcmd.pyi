"""
``pmcmd`` — every registered shell command as a real attribute.

Runtime: ``shell/shell/shell_py_bind.c``, one proxy object per row in the
``.pm_metal_shell_cmds.*`` linker section (mirrors ``shell_cmd.c``'s own
walk). Deliberately the one module that does NOT carry the
``pymergetic.metal.*`` prefix — see docs/MICROPYTHON.md.

Each command runs synchronously, exactly like typing it at the console
(same argc/argv shape via ``str(arg)`` on each Python argument, same
``pm_metal_shell_out()`` output). If the command itself starts a background
task (e.g. ``py``), observe completion via ``pymergetic.metal.process.*``.

Names below are illustrative — the real attribute set is whatever is
registered at build time via PM_METAL_SHELL_CMD; no static stub can be
exhaustive until build-time .pyi generation lands (docs/TODO.md).
"""

from typing import Any

def __getattr__(name: str) -> Any: ...
