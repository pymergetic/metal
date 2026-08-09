# Public Metal C compile surface

**This directory is the only product header surface** for the µPy port and other
hosts. Do not `#include` private impl paths under `src/` or package-root leftovers
from product code.

Pattern matches wasmmod [`extmod/wasmmod/include/`](../../wasmmod/include/README.md):

| Layer | Metal | wasmmod |
|-------|-------|---------|
| Public headers | `include/pymergetic/metal/…` | `include/pm_*.h` |
| Impl | `src/pymergetic/metal/…` | `glue/` + `crates/pm` |
| Python | `typings/`, `glue/` nest | `wasmmod.c` faces |

Headers here are **hand-maintained public faces** (forge mod-sync is gone).
They are **tracked in git** — not wholesale-ignored.
