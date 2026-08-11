# Metal layout = wasmmod

**Base = wasmmod.** Copy it. Do not invent a parallel dialect.

**Kernel = code container** (same idea as a wasmmod pack): introspectable via the same inspect/self-desc model (`pymergetic.metal` ‖ `pymergetic.wasmmod`). Loaded packs and host self are the same kind of thing, different roots.

## Module registry law (locked)

**µPy modules are the registry** (`sys.modules` + packages). Same registration for
resident C/RS, wasmmod itself, and wasm/aot/elf packs:

1. Publish a µPy module
2. Put exports on it (Py gets them for free)
3. Soft-connect once (`PM_MOD_IMPORT` / `PM_MOD_NEED`) — cache the call edge

See wasmmod [`PACK.md`](../../wasmmod/docs/PACK.md) “One module law” and
[`pm_mod.h`](../../wasmmod/include/pm_mod.h). Metal `PM_METAL_REG_MOD` / RegMod
ring is a **frozen façade** — new work uses `pm_mod_*`; do not grow the ring.

## The point (wasmmod)

**Callee (impl):** one language per module — **C or Rust or Python**.  
**Caller:** **any** of C / Rust / Python.  
Py muscle still needs **C + RS bridges** (call into Py). C/RS muscle still needs **Py glue**. No “Py-only, native never calls it.”

```text
caller\callee | Py | C | RS
--------------+----+---+----
Py            |  ✓ | ✓ | ✓
C             |  ✓ | ✓ | ✓
RS            |  ✓ | ✓ | ✓
```

Same verbs everywhere via **µPy module publish + connect** (`pm_mod_*` /
faces). That is why `.pyi` and Rust faces exist — so callers work, not so every module has three brains.

## Trees

```text
include/pymergetic/metal/…/*.h     # public C API (callers + C ABI border)
src/pymergetic/metal/…/            # ONE impl: .c  OR  .rs  OR  .py
include/SYMBOLS.md                 # C ↔ RS ↔ Py names (what links)
port/                              # -I include; link src
crates/pymergetic_metal_*/         # per-module RS crates (product umbrella)
typings/pymergetic/metal/…/*.pyi   # Py caller stubs (matrix Stub column)
```

| Role | Where |
|------|--------|
| Impl (callee) | `src/` — exactly one of `.c` / `.rs` / `.py` |
| C callers | `#include` + link |
| RS callers | `crates/pymergetic_metal_*` + path faces in `src/…/__init__.rs` |
| Py callers | registered module + `.pyi` |

No twin impls in `src/`. Net headers are packaged (`net/<mod>/__init__.h`).
ASGI lives at `net/asgi/`. Full `pm_metal_<path>_*`.

## SMP (landed)

- N = ACPI MADT (`pm_metal_dev_acpi_cpu_count`); product refuses `n < 2`
- BIOS: INIT-SIPI · UEFI: EFI MP Services → each CPU `pm_metal_async_run_loop_cpu`
- QEMU `-smp` = host provisioning only (`SMP=` in board `build.mk`)
- Proof: BIOS+UEFI smoke at 2 and 4 vCPUs (`smp N online` + AP work)
