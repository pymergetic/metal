# Source tree

Target layout for pymergetic-metal. See [LAYERS.md](LAYERS.md) for roles and [NAMING.md](NAMING.md) for symbol prefixes.

**Rule:** `include/pymergetic/metal/<mod>.h` + `host/<plat>/pymergetic/metal/<mod>.c` (engine) + `guest/pymergetic/metal/<mod>.c` (orchestrator when symmetric). Modules listed **A–Z** under `metal/`. `port/` and `pm_hostinfo` are engine-only. `wasi/` sits in `host/<plat>/pymergetic/` (outside `metal/`).

**Legend:** `[stub]` directory present, not implemented · `[ref]` ideas in `backup/1st_try/` only

---

```
packages/metal/
│
├── include/                                 # contract — engine + orchestrator compile against this
│   ├── wasi/                                [stub] vendored WASI snapshot (syscall transport)
│   └── pymergetic/
│       ├── pm_vis.h                         [stub] orchestrator/engine only — RUNTIME vs DEBUG
│       ├── mod/                             [stub] wasm mod SDK (replaces export/)
│       │   └── pm_mod.h                     optional helpers; WIT world later
│       ├── export/                          [transitional] native .o mods only
│       │   └── pm_export_v1.h
│       └── metal/                           metal contract
│           ├── metal.h                      umbrella — `#include <pymergetic/metal/metal.h>`
│           ├── sys/
│           │   ├── pm_sys.h                 bootstrap exchange (`pm_metal_sys_*`)
│           │   └── hostinfo.h               engine-only publish (`pm_metal_sys_hostinfo_*`)
│           ├── orchestrator/                [stub]
│           │   ├── boot.h                   layout slots, boot report
│           │   ├── instance.h               FRESH / PERSIST handles
│           │   └── loader.h                 pm_mod_load / call / drop API
│           ├── memory/                      [stub] transitional → pm_mem + orchestrator/boot
│           │   ├── arena.h
│           │   ├── boot.h
│           │   ├── layout.h
│           │   └── …
│           ├── pm_mem.h                     [stub] arena, malloc, mmap, shmalloc shim
│           ├── pm_types.h                   [stub] slices, handles, ownership
│           ├── port/                        plat.h — probe contract (engine impl only)
│           │   └── plat.h
│           ├── posix.h                      [stub] libc floor on WASI
│           ├── registry.h                   [stub] type ids, WIT/layout schemas
│           └── vartree.h                    [stub] named var catalog
│
├── host/                                    # engine — native forks per target
│   ├── linux/
│   │   ├── CMakeLists.txt                   [stub]
│   │   ├── main.c                           [stub] engine entry (wasmtime runner)
│   │   └── pymergetic/
│   │       ├── metal/
│   │       │   ├── orchestrator/
│   │       │   │   └── mod_host.c           [stub] wasmtime instantiate / component link
│   │       │   ├── sys/
│   │       │   │   ├── hostinfo.c           publish bootstrap → /sys/pm
│   │       │   │   └── pm_sys.c             encode probe → /sys/pm
│   │       │   ├── pm_mem.c                 [stub]
│   │       │   ├── pm_types.c               [stub]
│   │       │   ├── port/                    plat.c
│   │       │   │   ├── plat.c
│   │       │   │   └── traits.h
│   │       │   ├── posix.c                  [stub]
│   │       │   ├── registry.c               [stub]
│   │       │   └── vartree.c                [stub]
│   │       └── wasi/                        [stub] syscall impl glue (wasmtime WASI)
│   │           └── wasi_impl.c
│   │
│   ├── zephyr/
│   │   ├── CMakeLists.txt                   [stub] from runtime/zephyr/
│   │   ├── prj.conf, Kconfig, boards/       [stub]
│   │   ├── src/main.c                       [stub]
│   │   └── pymergetic/
│   │       ├── metal/
│   │       │   ├── orchestrator/mod_host.c  [stub] WAMR instantiate / component link
│   │       │   ├── sys/hostinfo.c, pm_sys.c
│   │       │   ├── pm_mem.c                 [stub] layout/arena from memory/ + port TLSF
│   │       │   ├── pm_types.c, posix.c, registry.c, vartree.c
│   │       │   └── port/                    plat.c, efi_ram.c, traits.h, …
│   │       └── wasi/wasi_impl.c             [stub] WAMR WASI + preopens
│   │
│   ├── rump/
│   │   ├── CMakeLists.txt, main.c           [stub]
│   │   └── pymergetic/
│   │       ├── metal/                       [stub]
│   │       │   ├── sys/hostinfo.c, pm_sys.c
│   │       │   ├── …
│   │       │   └── port/plat.c
│   │       └── wasi/wasi_impl.c
│   │
│   └── unikraft/
│       ├── CMakeLists.txt, main.c           [stub]
│       └── pymergetic/
│           ├── metal/                       [stub]
│           │   ├── pm_hostinfo.c
│           │   ├── …
│           │   └── port/plat.c
│           └── wasi/wasi_impl.c
│
├── guest/                                   # orchestrator — portable wasm32-wasip1
│   ├── CMakeLists.txt                       [stub] builds orchestrator.wasm
│   └── pymergetic/
│       ├── main.c                           [stub] orchestrator entry → metal/orchestrator boot
│       └── metal/
│           ├── orchestrator/                [stub] policy
│           │   ├── boot.c                   layout report, arena sizing (uses pm_sys)
│           │   └── loader.c                 FRESH / PERSIST, vartree bind, instance load
│           ├── pm_mem.c                     [stub] malloc + mmap; shmalloc shim
│           ├── sys/pm_sys.c                 one-time fd_read /sys/pm at init → cached getters
│           ├── pm_types.c                   [stub]
│           ├── posix.c                      [stub] wasi-libc floor
│           ├── registry.c                   [stub]
│           └── vartree.c                    [stub] named live catalog
│
├── mods/                                    [stub] mod instances — wasm32-wasip1; `mod/` only
├── apps/                                    [stub] app instances — python / rust / cpp
├── scripts/                                 [stub]
├── docs/
├── external/                                [stub] west deps (zephyr, …)
├── west-manifest/west.yml
├── stubs/
├── vfs/image/
│
└── backup/1st_try/                          [ref] prior experiments — not built from
```

---

## Module map (read across)

| module | `include/pymergetic/metal/` | engine `host/<plat>/…/metal/` | orchestrator `guest/…/metal/` |
|--------|-----------------------------|-------------------------------|-------------------------------|
| orchestrator | `orchestrator/*.h` | `orchestrator/mod_host.c` | `orchestrator/boot.c`, `loader.c` |
| pm_mem | `pm_mem.h` | `pm_mem.c` | `pm_mem.c` |
| sys | `sys/pm_sys.h`, `sys/hostinfo.h` | `sys/pm_sys.c`, `sys/hostinfo.c` | `sys/pm_sys.c` |
| pm_types | `pm_types.h` | `pm_types.c` | `pm_types.c` |
| posix | `posix.h` | `posix.c` | `posix.c` |
| registry | `registry.h` | `registry.c` | `registry.c` |
| vartree | `vartree.h` | `vartree.c` | `vartree.c` |
| port | `port/plat.h` | `port/plat.c`, `efi_ram.c`, … | — |
| wasi | `include/wasi/` | `../wasi/wasi_impl.c` | wasi-libc (linked) |

`sys/hostinfo` is engine-only (no guest `.c`). `port/` is engine-only. `wasi/` lives outside `metal/`. **Instances** (mods, apps) are separate wasm trees under `mods/`, `apps/`.

Prior experiment code lives in `backup/1st_try/` for reference when implementing each module.
