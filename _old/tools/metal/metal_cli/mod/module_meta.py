"""``.pm/module`` JSON metadata — replaces root ``.module`` / ``.package`` / ``.nomodule``."""
from __future__ import annotations

import json
from enum import Enum
from pathlib import Path

from metal_cli.paths import exp2_root

PM_DIR = ".pm"
PM_MODULE_FILE = "module"
PM_BUILD_STEM = "build"
PM_SMOKE_STEM = "smoke"

# Leftover root markers (must not remain after migration)
LEGACY_MARKERS = (".module", ".package", ".nomodule")


class ModuleType(str, Enum):
    MODULE = "module"
    PACKAGE = "package"
    HIDDEN = "hidden"


def src_roots() -> list[Path]:
    """Live modules live under exp2 only (old product archived under ``_old/``)."""
    return [exp2_root() / "src"]


def pm_dir(mod_dir: Path) -> Path:
    return mod_dir / PM_DIR


def module_json_path(mod_dir: Path) -> Path:
    return pm_dir(mod_dir) / PM_MODULE_FILE


def cargo_toml_path(mod_dir: Path) -> Path:
    """Rust crate manifest lives under ``.pm/``."""
    return pm_dir(mod_dir) / "Cargo.toml"


def parse_module_json(path: Path) -> dict[str, str]:
    """Load ``.pm/module`` JSON into a flat string dict (includes ``type``)."""
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"{path}: module JSON must be an object")
    out: dict[str, str] = {}
    for k, v in data.items():
        if v is None:
            continue
        if isinstance(v, (str, int, float, bool)):
            out[str(k)] = str(v) if not isinstance(v, str) else v
        else:
            raise ValueError(f"{path}: unsupported value for {k!r}")
    if "type" not in out:
        raise ValueError(f"{path}: missing type")
    try:
        ModuleType(out["type"])
    except ValueError as e:
        raise ValueError(
            f"{path}: bad type={out['type']!r} (want module|package|hidden)"
        ) from e
    return out


def load_module_meta(mod_dir: Path) -> dict[str, str]:
    path = module_json_path(mod_dir)
    if not path.is_file():
        raise FileNotFoundError(f"no {PM_DIR}/{PM_MODULE_FILE} in {mod_dir}")
    return parse_module_json(path)


def module_type_of(mod_dir: Path) -> ModuleType | None:
    path = module_json_path(mod_dir)
    if not path.is_file():
        return None
    return ModuleType(parse_module_json(path)["type"])


def discover_pm_dirs() -> list[Path]:
    """Every directory that has ``.pm/module`` (any type)."""
    found: list[Path] = []
    for root in src_roots():
        if not root.is_dir():
            continue
        for p in root.rglob(f"{PM_DIR}/{PM_MODULE_FILE}"):
            if p.is_file():
                found.append(p.parent.parent)
    return sorted(set(found))


def discover_modules() -> list[Path]:
    """``type=module`` only (kernel modules for sync/build/test)."""
    out: list[Path] = []
    for d in discover_pm_dirs():
        if module_type_of(d) == ModuleType.MODULE:
            out.append(d)
    return out


def discover_packages() -> list[Path]:
    out: list[Path] = []
    for d in discover_pm_dirs():
        if module_type_of(d) == ModuleType.PACKAGE:
            out.append(d)
    return out


def discover_hidden() -> list[Path]:
    """Former ``.nomodule`` trees."""
    out: list[Path] = []
    for d in discover_pm_dirs():
        if module_type_of(d) == ModuleType.HIDDEN:
            out.append(d)
    return out


def discover_nomodules() -> list[Path]:
    """Alias for discover_hidden (call-site compatibility)."""
    return discover_hidden()


def legacy_markers_under(root: Path) -> list[Path]:
    """Root ``.module`` / ``.package`` / ``.nomodule`` files still present."""
    found: list[Path] = []
    if not root.is_dir():
        return found
    for name in LEGACY_MARKERS:
        for p in root.rglob(name):
            if p.is_file() and p.name in LEGACY_MARKERS:
                found.append(p)
    return sorted(found)


def is_hidden_dir(d: Path) -> bool:
    return module_type_of(d) == ModuleType.HIDDEN
