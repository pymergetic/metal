"""Host-only helpers for forge config *.py.

Keep a single ``Any`` handle to host ``os`` so attribute checks do not assume
a guest/flat ``os`` stub.
"""
from __future__ import annotations

import os as _os_mod
from pathlib import Path
from typing import Any, MutableMapping

_host: Any = _os_mod


def metal_root(anchor: Path) -> Path:
    """Resolve metal package root from METAL_ROOT or path depth of *anchor*."""
    env = _host.getenv("METAL_ROOT")
    if env:
        return Path(env).resolve()
    # .../src/pymergetic/metal/forge/_kconfig/<file>.py -> metal root
    return anchor.resolve().parents[5]


def enter_config(config_dir: Path) -> None:
    """Set Kconfig ``srctree`` and chdir into config (host)."""
    _host.environ.setdefault("srctree", str(config_dir))
    _host.chdir(config_dir)


# Back-compat alias
enter_exp2_config = enter_config


def host_environ_with(metal_root_path: Path) -> MutableMapping[str, str]:
    """Copy of host environ plus METAL_ROOT (for spawning confgen)."""
    env: MutableMapping[str, str] = dict(_host.environ)
    env["METAL_ROOT"] = str(metal_root_path)
    return env
