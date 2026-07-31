"""Host-only helpers for forge config *.py.

Repo ``typings/os.pyi`` is Metal's *guest* ``os`` (flat FS, no environ/chdir).
Pyright applies that stub project-wide, so host scripts must not attribute-
check against ``import os``. Keep a single ``Any`` handle to the real host
module instead.
"""
from __future__ import annotations

import os as _os_mod
from pathlib import Path
from typing import Any, MutableMapping

# Runtime: CPython os. Types: guest stub — treat as Any for host APIs.
_host: Any = _os_mod


def metal_root(anchor: Path) -> Path:
    """Resolve metal package root from METAL_ROOT or path depth of *anchor*."""
    env = _host.getenv("METAL_ROOT")
    if env:
        return Path(env).resolve()
    # .../exp2/src/pymergetic/metal/forge/_kconfig/<file>.py -> metal root
    return anchor.resolve().parents[6]


def enter_exp2_config(config_dir: Path) -> None:
    """Set Kconfig ``srctree`` and chdir into exp2/config (host)."""
    _host.environ.setdefault("srctree", str(config_dir))
    _host.chdir(config_dir)


def host_environ_with(metal_root_path: Path) -> MutableMapping[str, str]:
    """Copy of host environ plus METAL_ROOT (for spawning confgen)."""
    env: MutableMapping[str, str] = dict(_host.environ)
    env["METAL_ROOT"] = str(metal_root_path)
    return env
