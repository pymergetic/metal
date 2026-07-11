# Symbol naming

Canonical rule for **public** C symbols (functions, types, macros). Read this before adding headers or APIs.

See also: [LAYERS.md](LAYERS.md) (roles) · [SOURCETREE.md](SOURCETREE.md) (paths).

---

## Module path → prefix

Derive the prefix from the **contract header path** under `include/pymergetic/metal/`:

```
include/pymergetic/metal/<path>.h  →  pm_metal_<path_with_slashes_as_underscores>_
```

| Contract header | Prefix | Example |
|-----------------|--------|---------|
| `metal/sys/sys.h` | `pm_metal_sys_` | `pm_metal_sys_init()` |
| `metal/util/fourcc.h` | `pm_metal_util_fourcc_` | `PM_METAL_UTIL_FOURCC_LE('P','M','S','Y')` |
| `metal/util/eightcc.h` | `pm_metal_util_eightcc_` | `PM_METAL_UTIL_EIGHTCC_LE('P','Y','M','E','T','A','L','!')` |
| `metal/util/endian.h` | `pm_metal_util_endian_` | `pm_metal_util_endian_load_u32_le()` |
| `metal/util/version.h` | `pm_metal_util_version_` | `PM_METAL_UTIL_VERSION_MAKE(1, 2, 3)` |
| `metal/util/versiontag.h` | `pm_metal_util_versiontag_` | `PM_METAL_UTIL_VERSIONTAG(ver, built)` |
| `metal/util/wiretag.h` | `pm_metal_util_wiretag_` | `PM_METAL_UTIL_WIRETAG(magic, version)` |
| `metal/util/bigtag.h` | `pm_metal_util_bigtag_` | `PM_METAL_UTIL_BIGTAG(magic, version, built)` |
| `metal/sys/hostinfo.h` | `pm_metal_sys_hostinfo_` | `pm_metal_sys_hostinfo_publish()`, `pm_metal_sys_hostinfo_load()` |
| `metal/port/plat.h` | `pm_metal_port_` | `pm_metal_port_machine_ram()` (kernel only) |
| `metal/orchestrator/boot.h` | `pm_metal_orchestrator_` | `pm_metal_orchestrator_boot()` |
| `metal/mem/mem.h` | `pm_metal_mem_` | `pm_metal_mem_alloc()` (planned) |
| `metal/build.h` | `PM_METAL_BUILD_` | `PM_METAL_BUILD_KERNEL` |
| `metal/export.h` | `PM_METAL_` | `PM_METAL_API`, `PM_METAL_KERNEL_API` |

**Umbrella:** `metal/metal.h` — convenience include only; no extra symbols.

---

## Filenames vs symbols

Directory names carry the module (`sys/`, `mem/`, `util/`). **Filenames omit redundant prefixes** (`sys.h`, `mem.h`, `hostinfo.h`). Public symbols still use the full `pm_metal_<path>_` prefix from the header path.

**Do not** use legacy `pm_<module>.h` filenames (`pm_sys.h`, `pm_mem.h`) — use `sys/sys.h`, `mem/mem.h`.

---

## Visibility macros (`metal/export.h`, `metal/build.h`)

Public API is **always emitted** — mod builds need no extra define.

Kernel-only declarations use `PM_METAL_KERNEL_API` and appear only when `PM_METAL_BUILD_KERNEL` is defined:

```c
/* metal/sys/sys.h */
PM_METAL_API(int, pm_metal_sys_machine_ram, (void));                    /* always */
PM_METAL_KERNEL_API(int, pm_metal_sys_bootstrap_encode,
                    (pm_metal_sys_bootstrap_t *out));                   /* kernel only */
```

Privileged mods compile with `-DPM_METAL_BUILD_KERNEL` — same headers, same wasm format.

---

## Implementation layout

| Role | Source |
|------|--------|
| Contract | `include/pymergetic/metal/…/*.h` |
| Upper | `src/pymergetic/metal/…/*.c` |
| Lower shared util | `host/common/pymergetic/metal/…/*.c` |
| Lower per target | `host/<plat>/pymergetic/metal/…/*.c` |

`sys/` groups bootstrap exchange: `hostinfo` (publish kernel-only, load public) + shared `sys.h` types/validate. Upper `sys/sys.c` receives bootstrap in-process — it does not call `hostinfo_load`.

Lower-only modules: `port/`, `wasi/` impl, `orchestrator/wasm_host.c`, `sys/hostinfo.c`, `sys/sys.c`.

---

## One-sided vs symmetric modules

| Module | Upper `.c` | Lower `.c` |
|--------|------------|------------|
| orchestrator/boot, loader | `src/…/orchestrator/` | — |
| orchestrator/wasm_host | — | `host/<plat>/…/orchestrator/wasm_host.c` |
| sys (cache) | `src/…/sys/sys.c` | `host/<plat>/…/sys/sys.c` (encode) |
| sys/hostinfo | `host/linux/…/sys/hostinfo.c` (POSIX) | `host/zephyr/…/sys/hostinfo.c` (Zephyr VFS) |
| sys/sys | `host/linux/…/sys/sys.c` | `host/zephyr/…/sys/sys.c` (firmware) |
| port/plat | `host/linux/…/port/plat.c` | `host/zephyr/…/port/plat.c` (firmware) |
| mem, types, registry, vartree, posix | `src/…/` | — |
| port | — | `host/<plat>/…/port/` |
| wasi | — | `host/<plat>/…/wasi/` |
| util | — | `host/common/…/util/` |

---

## What does not get prefixed

| Kind | Rule |
|------|------|
| `static` helpers in `.c` | Short local names OK |
| `main()` | Entry symbol |
| POSIX / WASI / libc | Unchanged |

---

## Checklist (new API)

1. Place contract in `include/pymergetic/metal/<module>/` matching [SOURCETREE.md](SOURCETREE.md).
2. Prefix every public symbol with the table above.
3. Gate kernel-only API with `PM_METAL_KERNEL_API` in `metal/export.h`.
4. Upper `.c` in `src/`; lower `.c` in `host/<plat>/` (or `host/common/` for util).
5. Grep for retired paths (`guest/`, `pm_sys.h`, `mod_host`, `orchestrator.wasm`).

---

## Current slice (implemented / migrating)

| Symbol | Status |
|--------|--------|
| `pm_metal_sys_*` | ✓ bootstrap exchange (path migration in progress) |
| `pm_metal_sys_hostinfo_publish` | ✓ lower publish to `/sys/pm` |
| `pm_metal_sys_hostinfo_load` | ✓ wasm mod load from `/sys/pm` |
| `pm_metal_port_*` | ✓ lower probes |
| `pm_metal_orchestrator_boot` | ✓ upper boot (migrating from guest wasm to `src/`) |
