# Module registry SoT

**Face:** `sys.modules` (+ builtins) — normal import / Py attrs.  
**Our metadata:** global `__pm_modules` — same dict shape as `sys.modules`.

```text
sys.modules[name]   → face module
__pm_modules[name]  → { container, native: { func: ptr, ... } }
```

Soft connect reads `__pm_modules` once → caches in IMPORT/NEED slot.  
Never write pm_mod bookkeeping onto face modules.

See [`pm_mod.h`](../../wasmmod/include/pm_mod.h).
