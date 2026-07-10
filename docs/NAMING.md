# Symbol naming

Canonical rule for **public** C symbols (functions, types, macros). Read this before adding headers or APIs.

See also: [LAYERS.md](LAYERS.md) (roles) · [SOURCETREE.md](SOURCETREE.md) (paths).

---

## Module path → prefix

Derive the prefix from the **contract header path** under `include/pymergetic/`:

```
include/pymergetic/<path>.h  →  pm_<path_with_slashes_as_underscores>_
```

| Contract header | Prefix | Example |
|-----------------|--------|---------|
| `metal/sys/sys.h` | `pm_metal_sys_` | `pm_metal_sys_init()` |
| `metal/util/fourcc.h` | `pm_metal_util_fourcc_` | `PM_METAL_UTIL_FOURCC_LE('P','M','S','Y')` |
| `metal/util/endian.h` | `pm_metal_util_endian_` | `pm_metal_util_endian_load_u32_le()` |
| `metal/util/version.h` | `pm_metal_util_version_` | `PM_METAL_UTIL_VERSION_MAKE(1, 2, 3)` |
| `metal/util/wiretag.h` | `pm_metal_util_wiretag_` | `PM_METAL_UTIL_WIRETAG(magic, version)` |
| `metal/sys/hostinfo.h` | `pm_metal_sys_hostinfo_` | `pm_metal_sys_hostinfo_publish()` |
| `metal/sys/guestinfo.h` | `pm_metal_sys_guestinfo_` | `pm_metal_sys_guestinfo_load()` |
| `metal/port/plat.h` | `pm_metal_port_` | `pm_metal_port_machine_ram()` |
| `metal/orchestrator/boot.h` | `pm_metal_orchestrator_` | `pm_metal_orchestrator_boot()` |
| `metal/pm_mem.h` | `pm_metal_mem_` | `pm_metal_mem_alloc()` (planned) |
| `mod/pm_mod.h` | `pm_mod_` | instance entrypoints (planned) |

**Umbrella:** `metal/metal.h` — convenience include only; no extra symbols.

### Filenames vs symbols

Directory names carry the module (`sys/`, `util/`). **Filenames omit the `pm_` prefix** (`sys.h`, `hostinfo.c`). Public symbols still use the full `pm_metal_<path>_` prefix from the header path.

### `pm_<foo>.h` under `metal/`

Legacy docs used `pm_sys.h` — **do not** double the `pm_` in symbol names:

- `metal/sys/sys.h` → `pm_metal_sys_*` ✓

### Engine-only metal modules

Some metal modules are **one-sided** (no symmetric guest/host `.c`):

- `sys/hostinfo` — engine publish
- `sys/guestinfo` — orchestrator load
- `port/`, `orchestrator/mod_host.c` — engine only

---

## Types and macros

Same path rule:

```c
/* metal/sys/sys.h */
#define PM_METAL_SYS_MAGIC PM_METAL_UTIL_FOURCC_LE('P', 'M', 'S', 'Y')
typedef struct pm_metal_sys_bootstrap { … } pm_metal_sys_bootstrap_t;
/* bootstrap.tag — pm_metal_util_wiretag_t (magic + version) */

/* metal/port/plat.h */
typedef enum pm_metal_port_target_id { … } pm_metal_port_target_id_t;
```

---

## What does not get prefixed

| Kind | Rule |
|------|------|
| `static` helpers in `.c` | Short local names OK (`mkdir_p`, `parse_u64_env`) |
| `main()` | Entry symbol |
| POSIX / WASI / libc | Unchanged |

---

## Implementation layout (not the prefix)

| Role | Source |
|------|--------|
| Contract | `include/pymergetic/metal/…/*.h` |
| Engine common | `host/common/pymergetic/metal/…/*.c` (shared across `host/<plat>/`) |
| Engine metal | `host/<plat>/pymergetic/metal/…/*.c` |
| Orchestrator metal | `guest/pymergetic/metal/…/*.c` |

`sys/` groups bootstrap exchange: `hostinfo` (engine publish) + `guestinfo` (orchestrator load) + shared `sys.h` types/validate.

---

## Checklist (new API)

1. Place contract in `include/pymergetic/metal/…` matching [SOURCETREE.md](SOURCETREE.md).
2. Prefix every public symbol with the table above.
3. Grep for old paths (`metal/pm_sys.h`, `pm_metal_hostinfo_*`, `pm_plat_*`, …).
4. Run `./scripts/verify-slice.sh` when touching the first slice.

---

## Current slice (implemented)

| Symbol | Status |
|--------|--------|
| `pm_metal_sys_*` | ✓ bootstrap exchange |
| `pm_metal_sys_hostinfo_publish` | ✓ engine publish to `/sys/pm` |
| `pm_metal_sys_guestinfo_load` | ✓ orchestrator load from `/sys/pm` |
| `pm_metal_port_*` | ✓ engine probes |
| `pm_metal_orchestrator_boot` | ✓ guest boot |
