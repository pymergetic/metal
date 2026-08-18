"""pymergetic.metal.net.microdot._utemplate — vendored utemplate engine.

Micro, memory-efficient template engine by Paul Sokolovsky (MIT). Upstream:
`pfalcon/utemplate`. Kept under `_utemplate` so it does not collide with the
microdot `utemplate.py` extension module next to it. Loaders: `compiled`,
`source`, `recompile`.
"""

from . import compiled
from . import recompile
from . import source

__all__ = ["compiled", "source", "recompile"]
__version__ = "1.4.1"
