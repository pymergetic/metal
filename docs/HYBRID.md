# Metal layout = wasmmod

**Base = wasmmod.** Copy it. Do not invent a parallel dialect.

**Kernel = code container** (same idea as a wasmmod pack): introspectable via the same inspect/self-desc model (`pymergetic.metal` ‖ `pymergetic.wasmmod`). Loaded packs and host self are the same kind of thing, different roots.

## The point (wasmmod)

**Callee (impl):** one language per module — **C or Rust or Python**.  
**Caller:** **any** of C / Rust / Python.

```text
caller\callee | Py | C | RS
--------------+----+---+----
Py            |  ✓ | ✓ | ✓
C             |  ✓ | ✓ | ✓
RS            |  ✓ | ✓ | ✓
```

Same verbs everywhere via **SYMBOLS + registration** (`module_install` / faces). That is why `.pyi` and Rust faces exist — so callers work, not so every module has three brains.

## Trees

```text
include/pymergetic/metal/…/*.h     # public C API (callers + C ABI border)
src/pymergetic/metal/…/            # ONE impl: .c  OR  .rs  OR  .py
crates/pm_metal/                   # Rust caller façade (bindgen / wraps) — Cargo root
typings/pymergetic/metal/…/*.pyi   # Python caller stubs
include/SYMBOLS.md                 # C ↔ RS ↔ Py names
port/                              # -I include; link src
```

| Role | Where |
|------|--------|
| Impl (callee) | `src/` — exactly one of `.c` / `.rs` / `.py` |
| C callers | `#include` + link |
| RS callers | `crates/pm_metal` → same verbs |
| Py callers | registered module + `.pyi` |

No twin impls in `src/`. ASGI under `asgi/`, not `net/`. Full `pm_metal_<path>_*`.
