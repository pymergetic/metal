"""pymergetic.metal.console — package marker. Runtime is µPy C."""

from typing import Any


def __getattr__(name: str) -> Any:
    raise AttributeError(name)
