#!/usr/bin/env python3
"""Refresh exp2/config/.config after Kconfig edits (forge-private; forge config old)."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from _util import enter_exp2_config, host_environ_with, metal_root


def main() -> int:
    root = metal_root(Path(__file__))
    exp2 = root / "exp2"
    klib = root / "scripts" / "lib" / "kconfig"
    config_dir = exp2 / "config"
    dotconfig = config_dir / ".config"
    defconfig = config_dir / "defconfig"
    confgen = Path(__file__).resolve().parent / "confgen.py"

    sys.path.insert(0, str(klib))
    enter_exp2_config(config_dir)
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
