# Source tree (proposed)

Target layout for pymergetic-metal. See [LAYERS.md](LAYERS.md) for the layer model and module matrix.

**Rule:** `include/pymergetic/metal/<mod>.h` + `host/<plat>/pymergetic/metal/<mod>.c` + `guest/pymergetic/metal/<mod>.c` (same module, three places). Modules listed **A–Z** under `metal/`. `port/` is host-only but lives in `metal/port/`. `pm_host.c` and `wasi/` sit in `host/<plat>/pymergetic/` (outside `metal/`).

**Legend:** `[today]` exists now · `[migrate]` moves from current path · `[new]` not implemented yet

---

```
packages/metal/
│
├── include/                                 # contract — host + guest compile against this
│   ├── wasi/                                [new] vendored WASI snapshot (syscall transport)
│   └── pymergetic/
│       ├── pm_vis.h                         [today] orchestrator/host only — RUNTIME vs DEBUG
│       ├── mod/                             [new] wasm mod SDK (replaces export/)
│       │   └── pm_mod.h                     optional helpers; WIT world later
│       ├── export/                          [transitional] native .o mods only
│       │   └── pm_export_v1.h
│       └── metal/                           orchestrator guest stack
│           ├── metal.h                      [today] umbrella
│           ├── orchestrator/                [new]
│           │   ├── boot.h                   layout slots, boot report
│           │   ├── instance.h               FRESH / PERSIST handles
│           │   └── loader.h                 pm_mod_load / call / drop API
│           ├── memory/                      [today] transitional → pm_mem + orchestrator/boot
│           │   ├── arena.h
│           │   ├── boot.h
│           │   ├── layout.h
│           │   └── …
│           ├── pm_mem.h                     [new] arena, malloc, mmap, shmalloc shim
│           ├── pm_sys.h                     [new] machine_ram, arena_budget, exchange types
│           ├── pm_types.h                   [new] slices, handles, ownership
│           ├── port/                          [new] plat.h — probe contract (host impl only)
│           │   └── plat.h
│           ├── posix.h                      [new] libc floor on WASI
│           ├── registry.h                   [new] type ids, WIT/layout schemas
│           └── vartree.h                    [new] named var catalog
│
├── host/                                    # native — forks per target
│   ├── linux/
│   │   ├── CMakeLists.txt                   [migrate] from runtime/linux/
│   │   ├── main.c                           [migrate] host entry (wasmtime runner)
│   │   └── pymergetic/
│   │       ├── metal/
│   │       │   ├── orchestrator/
│   │       │   │   └── mod_host.c           [new] wasmtime instantiate / component link
│   │       │   ├── pm_mem.c                 [new]
│   │       │   ├── pm_sys.c                 [new] encode probe → /sys/pm
│   │       │   ├── pm_types.c               [new]
│   │       │   ├── port/                    [migrate] from src/pymergetic/port/linux/
│   │       │   │   ├── plat.c
│   │       │   │   └── traits.h
│   │       │   ├── posix.c                  [new]
│   │       │   ├── registry.c               [new]
│   │       │   └── vartree.c                [new]
│   │       ├── wasi/                        [new] syscall impl glue (wasmtime WASI)
│   │       │   └── wasi_impl.c
│   │       └── pm_host.c                    [new] preopened /sys/pm VFS writer
│   │
│   ├── zephyr/
│   │   ├── CMakeLists.txt                   [migrate] from runtime/zephyr/
│   │   ├── prj.conf, Kconfig, boards/       [migrate]
│   │   ├── src/main.c                       [migrate]
│   │   └── pymergetic/
│   │       ├── metal/
│   │       │   ├── orchestrator/mod_host.c  [new] WAMR instantiate / component link
│   │       │   ├── pm_mem.c                 [migrate] layout/arena from memory/ + port TLSF
│   │       │   ├── pm_sys.c                 [new] encode port probes → /sys/pm
│   │       │   ├── pm_types.c, posix.c, registry.c, vartree.c
│   │       │   └── port/                    [migrate] plat.c, efi_ram.c, traits.h, …
│   │       ├── wasi/wasi_impl.c             [new] WAMR WASI + preopens
│   │       └── pm_host.c                    [new]
│   │
│   ├── rump/
│   │   ├── CMakeLists.txt, main.c           [new]
│   │   └── pymergetic/
│   │       ├── metal/                       [new] stub
│   │       │   ├── …
│   │       │   └── port/plat.c
│   │       ├── pm_host.c
│   │       └── wasi/wasi_impl.c
│   │
│   └── unikraft/
│       ├── CMakeLists.txt, main.c           [new]
│       └── pymergetic/
│           ├── metal/                       [new] stub
│           │   ├── …
│           │   └── port/plat.c
│           ├── pm_host.c
│           └── wasi/wasi_impl.c
│
├── guest/                                   # portable wasm32-wasip1 — one tree, all targets
│   ├── CMakeLists.txt                       [new] builds pymergetic.wasm
│   └── pymergetic/
│       ├── main.c                           [new] guest entry → metal/orchestrator boot
│       └── metal/
│           ├── orchestrator/                [new] policy
│           │   ├── boot.c                   layout report, arena sizing (uses pm_sys)
│           │   └── loader.c                 FRESH / PERSIST, vartree bind, mod calls
│           ├── pm_mem.c                     [new] malloc + mmap; shmalloc shim
│           ├── pm_sys.c                     [new] one-time fd_read /sys/pm at init → cached getters
│           ├── pm_types.c                   [new]
│           ├── posix.c                      [new] wasi-libc floor
│           ├── registry.c                   [new]
│           └── vartree.c                    [new] named live catalog
│
├── apps/                                    [new] python / rust / cpp
├── mods/                                    [today] wasm32-wasip1; `mod/` only (no pm_vis)
├── scripts/                                 [today]
├── docs/
├── external/                                [today] west deps (zephyr, …)
├── west-manifest/west.yml
├── stubs/
├── vfs/image/
│
│── # transitional — remove after migrate
├── src/pymergetic/                          [today] → host/<plat>/pymergetic/
│   ├── metal/memory/
│   ├── platform/plat.h
│   └── port/{linux,zephyr}/                 → host/<plat>/pymergetic/metal/port/
└── runtime/                                 [today] → host/<plat>/
    ├── linux/
    └── zephyr/
```

---

## Module map (read across)

| module | `include/pymergetic/metal/` | `host/<plat>/pymergetic/metal/` | `guest/pymergetic/metal/` |
|--------|-----------------------------|----------------------------------|---------------------------|
| orchestrator | `orchestrator/*.h` | `orchestrator/mod_host.c` | `orchestrator/boot.c`, `loader.c` |
| pm_mem | `pm_mem.h` | `pm_mem.c` | `pm_mem.c` |
| pm_sys | `pm_sys.h` | `pm_sys.c` | `pm_sys.c` |
| pm_types | `pm_types.h` | `pm_types.c` | `pm_types.c` |
| posix | `posix.h` | `posix.c` | `posix.c` |
| registry | `registry.h` | `registry.c` | `registry.c` |
| vartree | `vartree.h` | `vartree.c` | `vartree.c` |
| port | `port/plat.h` | `port/plat.c`, `efi_ram.c`, … | — |
| wasi | `include/wasi/` | `../wasi/wasi_impl.c` | wasi-libc (linked) |
| pm_host | — | `../pm_host.c` | — |

`pm_host.c` and `wasi/` live in `host/<plat>/pymergetic/` — siblings of `metal/`. `port/` is inside `metal/` (host impl only).

---

## Migrate notes

| From (today) | To (proposed) |
|--------------|---------------|
| `include/pymergetic/metal/memory/` | stays; folds into `pm_mem` + `orchestrator/boot` over time |
| `include/pymergetic/metal/*.h` (new modules) | `include/pymergetic/metal/pm_*.h`, `orchestrator/*.h` |
| `include/pymergetic/export/` | `include/pymergetic/mod/` (wasm SDK) |
| `src/pymergetic/metal/memory/` | `guest/.../orchestrator/boot.c` + `guest/.../pm_mem.c` + `host/.../pm_sys.c` (+ host `pm_mem.c` transitional) |
| `src/pymergetic/port/` | `host/<plat>/pymergetic/metal/port/` |
| `runtime/<plat>/` | `host/<plat>/` (app shell + boards) |

`external/zephyr/` stays outside `host/` — vanilla west dependency, not pymergetic code.
