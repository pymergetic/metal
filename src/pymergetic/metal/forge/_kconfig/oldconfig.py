#!/usr/bin/env python3
"""Refresh config/.config after Kconfig edits (forge-private; forge config old)."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from _util import enter_config, host_environ_with, metal_root


def main() -> int:
    root = metal_root(Path(__file__))
    klib = Path(__file__).resolve().parent
    config_dir = root / "config"
    dotconfig = config_dir / ".config"
    defconfig = config_dir / "defconfig"
    confgen = Path(__file__).resolve().parent / "confgen.py"

    sys.path.insert(0, str(klib))
    enter_config(config_dir)
    if not dotconfig.is_file() and defconfig.is_file():
        dotconfig.write_bytes(defconfig.read_bytes())

    import kconfiglib

    kconf = kconfiglib.Kconfig("Kconfig", warn_to_stderr=True)
    if dotconfig.is_file():
        kconf.load_config(str(dotconfig))
    kconf.write_config(str(dotconfig))
    return subprocess.call(
        [sys.executable, str(confgen)],
        env=host_environ_with(root),
    )


if __name__ == "__main__":
    sys.exit(main())
