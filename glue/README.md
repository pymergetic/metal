# glue/ — mirrors `include/pymergetic/metal/`

Thin µPy faces for C/RS callees under `src/`. Same law as wasmmod
[`extmod/wasmmod/glue/README.md`](../../wasmmod/glue/README.md):

```text
include/pymergetic/metal/util/lz4/__init__.h   # C ABI
src/pymergetic/metal/util/lz4/__init__.rs      # callee (one lang)
glue/pymergetic/metal/util/lz4.c               # µPy nest face
typings/pymergetic/metal/util/lz4.pyi          # typing only
→ import pymergetic.metal.util.lz4
```

**Not** product callees. **Not** frozen reexports. **No** private `_pm_*`
builtins. **No** fuckname aliases (`metalnet`, `ssh`, …).

Root registration: `glue/pymergetic/__init__.c` → `MP_REGISTER_MODULE(pymergetic)`.
Leaves nest via `MICROPY_MODULE_BUILTIN_SUBPACKAGES`.

Lives at metal package root (like wasmmod) — **not** under `port/`.
