"""Checkout roots."""
from __future__ import annotations

from pathlib import Path

TOOLS_METAL = Path(__file__).resolve().parents[1]
METAL_ROOT = TOOLS_METAL.parents[1]


def metal_root() -> Path:
    return METAL_ROOT


def exp2_root() -> Path:
    return METAL_ROOT / "exp2"


def packages_dir() -> Path:
    return METAL_ROOT / "build" / "packages"
