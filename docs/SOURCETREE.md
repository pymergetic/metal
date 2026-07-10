# Source tree

Target layout for pymergetic-metal. See [LAYERS.md](LAYERS.md) for naming (engine / orchestrator / instance) and the module matrix.

**Rule:** `include/pymergetic/metal/<mod>.h` + `host/<plat>/pymergetic/metal/<mod>.c` (engine) + `guest/pymergetic/metal/<mod>.c` (orchestrator). Modules listed **A–Z** under `metal/`. `port/` is engine-only but lives in `metal/port/`. `pm_hostinfo.c` and `wasi/` sit in `host/<plat>/pymergetic/` (outside `metal/`).

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
│       └── metal/                           orchestrator metal contract
│           ├── metal.h                      [stub] umbrella
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
│           ├── pm_sys.h                     [stub] machine_ram, arena_budget, exchange types
│           ├── pm_types.h                   [stub] slices, handles, ownership
│           ├── port/                          [stub] plat.h — probe contract (engine impl only)
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
│   │       │   ├── pm_mem.c                 [stub]
│   │       │   ├── pm_sys.c                 [stub] encode probe → /sys/pm
│   │       │   ├── pm_types.c               [stub]
│   │       │   ├── port/                    [stub] from src/pymergetic/port/linux/
│   │       │   │   ├── plat.c
│   │       │   │   └── traits.h
│   │       │   ├── posix.c                  [stub]
│   │       │   ├── registry.c               [stub]
│   │       │   └── vartree.c                [stub]
│   │       ├── wasi/                        [stub] syscall impl glue (wasmtime WASI)
│   │       │   └── wasi_impl.c
│   │       └── pm_hostinfo.c                [stub] publish bootstrap blob → /sys/pm
│   │
│   ├── zephyr/
│   │   ├── CMakeLists.txt                   [stub] from runtime/zephyr/
│   │   ├── prj.conf, Kconfig, boards/       [stub]
│   │   ├── src/main.c                       [stub]
│   │   └── pymergetic/
│   │       ├── metal/
│   │       │   ├── orchestrator/mod_host.c  [stub] WAMR instantiate / component link
│   │       │   ├── pm_mem.c                 [stub] layout/arena from memory/ + port TLSF
│   │       │   ├── pm_sys.c                 [stub] encode port probes → /sys/pm
│   │       │   ├── pm_types.c, posix.c, registry.c, vartree.c
│   │       │   └── port/                    [stub] plat.c, efi_ram.c, traits.h, …
│   │       ├── wasi/wasi_impl.c             [stub] WAMR WASI + preopens
│   │       └── pm_hostinfo.c                [stub]
│   │
│   ├── rump/
│   │   ├── CMakeLists.txt, main.c           [stub]
│   │   └── pymergetic/
│   │       ├── metal/                       [stub] stub
│   │       │   ├── …
│   │       │   └── port/plat.c
│   │       ├── pm_hostinfo.c
│   │       └── wasi/wasi_impl.c
│   │
│   └── unikraft/
│       ├── CMakeLists.txt, main.c           [stub]
│       └── pymergetic/
│           ├── metal/                       [stub] stub
│           │   ├── …
│           │   └── port/plat.c
│           ├── pm_hostinfo.c
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
│           ├── pm_sys.c                     [stub] one-time fd_read /sys/pm at init → cached getters
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
| pm_sys | `pm_sys.h` | `pm_sys.c` | `pm_sys.c` |
| pm_types | `pm_types.h` | `pm_types.c` | `pm_types.c` |
| posix | `posix.h` | `posix.c` | `posix.c` |
| registry | `registry.h` | `registry.c` | `registry.c` |
| vartree | `vartree.h` | `vartree.c` | `vartree.c` |
| port | `port/plat.h` | `port/plat.c`, `efi_ram.c`, … | — |
| wasi | `include/wasi/` | `../wasi/wasi_impl.c` | wasi-libc (linked) |
| pm_hostinfo | — | `../pm_hostinfo.c` | — |

`pm_hostinfo.c` and `wasi/` live in `host/<plat>/pymergetic/` — siblings of `metal/`. `port/` is inside `metal/` (engine impl only). **Instances** (mods, apps) are separate wasm trees under `mods/`, `apps/`.

Prior experiment code lives in `backup/1st_try/` for reference when implementing each module.
