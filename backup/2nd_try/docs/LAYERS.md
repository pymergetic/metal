# Layers

```
targets = { linux, zephyr, rump, unikraft }
```

## Naming

**Pymergetic** is the whole system. In prose, use **role names**; in the tree, keep short dir names.

**Symbol prefixes** follow the contract header path — see **[NAMING.md](NAMING.md)**. Quick rule: `include/pymergetic/metal/<path>.h` → `pm_metal_<path>_…` (e.g. `metal/sys/sys.h` → `pm_metal_sys_init`).

| Role | Dir | What |
|------|-----|------|
| **Runtime** | `src/` + `host/` | One native binary per target: upper orchestrator + lower ports/WASI/wasm loader |
| **Upper** | `src/pymergetic/metal/` | Portable orchestrator policy — boot, layout, loader control (native, baked in) |
| **Lower** | `host/<plat>/pymergetic/metal/` | Platform bind — probes, hostinfo, WASI host, wasm instance loader |
| **Mod** | `mods/`, `apps/` | Wasm units the orchestrator loads after boot |

**Metal** — single contract under `include/pymergetic/metal/`. Mod authors compile against **headers only**; kernel links **headers + `src/` + `host/`**.

```
Pymergetic
├── Runtime (one binary: upper + lower)
│   ├── Upper (src/) — orchestrator boot, mem, loader
│   └── Lower (host/) — port, wasi, wasm_host
│       └── /sys/pm publish (for wasm mods)
└── Mods (mods/, apps/) — wasm below WASI line
```

One-liner: **Runtime** probes and runs native orchestrator; **mods** are wasm below the WASI boundary.

---

## Upper / lower / WASI

```
┌──────────────────────────────────────────────────────────┐
│  UPPER (src/) — orchestrator policy, native C            │
│  boot, mem, loader, sys, registry, vartree, …            │
├──────────────────────────────────────────────────────────┤
│  LOWER (host/) — port, hostinfo, wasi host, wasm_host     │
├──────────────────────────────────────────────────────────┤
│  WASI — binary format + syscall surface (clean cut)      │
├──────────────────────────────────────────────────────────┤
│  MODS — wasm (mods/, apps/); headers from include/metal/ │
└──────────────────────────────────────────────────────────┘
```

**WASI** is the runtime boundary for wasm guests — posix, vfs, clocks, env implemented in lower `wasi/`. The orchestrator upper is **native above** WASI; it **hosts** WASI, it does not execute through it.

**No orchestrator wasm.** One shipped binary (PXE, firmware, linux dev) — upper linked natively. `wasm_host` loads **mods only**.

---

## Visibility

Public metal API is **always visible** in headers — no extra define for mod builds.

Kernel-only symbols are gated by **`PM_METAL_BUILD_KERNEL`** (`metal/build.h`):

| Build | Defines | Sees |
|-------|---------|------|
| Mod `.wasm` | (none) | public API only |
| Privileged mod `.wasm` | `-DPM_METAL_BUILD_KERNEL` | public + kernel-gated API |
| Kernel binary | `-DPM_METAL_BUILD_KERNEL` | full API |

There is no separate mod SDK tree. A privileged mod is the same wasm format — compiled with kernel headers visible.

Macros live in `metal/export.h` (`PM_METAL_API`, `PM_METAL_KERNEL_API`). Optional debug tier: `PM_METAL_VIS_DEBUG` (see [PLATFORM.md](PLATFORM.md)).

---

## Repo layout

**Rule:** `include/pymergetic/metal/<module>/<name>.h` + `src/pymergetic/metal/<module>/` (upper) + `host/<plat>/pymergetic/metal/<module>/` (lower where needed). See [SOURCETREE.md](SOURCETREE.md).

```
┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│   module    │  │  include/pymergetic/metal/   │  │   src/…/metal/   (upper)    │  │ host/<plat>/…/metal/ (lower)│
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘

┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│ orchestrator│  │   orchestrator/*.h          │  │ orchestrator/boot.c,loader.c│  │  orchestrator/wasm_host.c   │
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘

┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│     mem     │  │        mem/mem.h            │  │         mem/mem.c           │  │                             │
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘

┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│     sys     │  │     sys/sys.h, hostinfo.h    │  │         sys/sys.c           │  │   sys/sys.c, hostinfo.c     │
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘

┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│    types    │  │       types/types.h         │  │        types/types.c        │  │                             │
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘

┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│    posix    │  │       posix/posix.h         │  │        posix/posix.c        │  │                             │
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘

┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│   registry  │  │     registry/registry.h     │  │     registry/registry.c     │  │                             │
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘

┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│   vartree   │  │     vartree/vartree.h       │  │     vartree/vartree.c       │  │                             │
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘

┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│    wasi     │  │        wasi/wasi.h          │  │                             │  │      wasi/*_impl.c          │
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘

┌─────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
│    port     │  │        port/plat.h           │  │                             │  │   port/plat.c, …          │
└─────────────┘  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘
```

**Upper** owns policy (`orchestrator/boot`, `mem`, loader control). **Lower** owns mechanism (`port/`, `wasi/`, `wasm_host`, `hostinfo` publish). See [SOURCETREE.md](SOURCETREE.md).

**Upper stack** — native C, identical sources on every target. Local memory first (`mem`: malloc, mmap), then type machinery (`registry` → `types`), named catalog (`vartree`), posix floor, orchestration. Loads **mods** via lower `wasm_host` after boot.

**Where things live:** cross-instance data uses the **component model shared blob** (lower). `shmalloc` is a **shim inside `mem`** that presents that blob as usable bytes/handles. `registry` holds WIT/type schemas; `types` lifts/lowers resources at boundaries. `vartree` is the live named var catalog above types.

---

## Per-target stack

```
┌────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐
│   target   │  │         linux          │  │         zephyr         │  │          rump          │  │        unikraft        │
└────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘

┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                                          HARDWARE                                                          │
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐
│    boot    │  │      GRUB / UEFI       │  │    EFI / multiboot     │  │    anyboot / loader    │  │         ukboot         │
└────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘

┌────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐
│   kernel   │  │      Linux kernel      │  │     Zephyr kernel      │  │      rump kernel       │  │     unikernel libs     │
└────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘

┌────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐
│  runtime   │  │  upper+lower (native)  │  │  upper+lower (native)  │  │  upper+lower (native)  │  │  upper+lower (native)  │
└────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘

┌────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐
│    wasm    │  │        wasmtime        │  │          WAMR          │  │   wasm engine (TBD)    │  │   wasm engine (TBD)    │
└────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘
                └─ wasm_host loads mods only; orchestrator upper is native ─┘

┌────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐
│  hostinfo  │  │ hostinfo → /sys/pm     │  │ hostinfo → /sys/pm     │  │   hostinfo (stub)      │  │   hostinfo (stub)      │
└────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘
                └─ for wasm mods; upper gets bootstrap in-process ─┘

┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                                            WASI                                                            │
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌────────────┐  ┌────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│     sys    │  │  public getters; wasm mods read /sys/pm via hostinfo_load; upper receives bootstrap pointer at boot       │
└────────────┘  └────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌────────────┐  ┌────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│     mem    │  │          malloc + mmap (local); shmalloc shim → component shared blob                                      │
└────────────┘  └────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌────────────┐  ┌────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│  registry  │  │                            type registry — ids, layouts, field schemas (const)                             │
└────────────┘  └────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌────────────┐  ┌────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│   types    │  │                                 runtime types — slices, handles, ownership                                 │
└────────────┘  └────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌────────────┐  ┌────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│  vartree   │  │                      named catalog — var headers, const char* name, linked list tree                       │
└────────────┘  └────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌────────────┐  ┌────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│   posix    │  │                     posix floor — fd, paths, env, clocks (wasm guests via WASI)                          │
└────────────┘  └────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌────────────┐  ┌────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│orchestrator│  │              boot report, layout slots, mod loader (native upper; wasm_host in lower)                      │
└────────────┘  └────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                              mods · apps (python · rust · c++ · …) — wasm below WASI                                       │
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```
