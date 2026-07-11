# Source tree

Target layout for pymergetic-metal. See [LAYERS.md](LAYERS.md) for roles and [NAMING.md](NAMING.md) for symbol prefixes.

**Rule:** contract in `include/pymergetic/metal/<module>/<name>.h`; upper `.c` in `src/pymergetic/metal/`; lower `.c` in `host/<plat>/pymergetic/metal/` (plus `host/common/pymergetic/metal/` for shared util). Everything lives under `pymergetic/metal/` — no parallel include roots.

**Legend:** `[stub]` directory present, not implemented · `[ref]` ideas in `backup/1st_try/` only

---

```
packages/metal/
│
├── include/pymergetic/metal/              # contract — mods compile against headers only
│   ├── metal.h                            # umbrella
│   ├── build.h                            # PM_METAL_BUILD_KERNEL
│   ├── export.h                              # PM_METAL_API, PM_METAL_KERNEL_API
│   │
│   ├── sys/
│   │   ├── sys.h
│   │   ├── hostinfo.h                    # publish (kernel) + load (wasm mod)
│   ├── orchestrator/
│   │   ├── boot.h
│   │   ├── instance.h
│   │   └── loader.h
│   ├── port/
│   │   └── plat.h                         # lower bind — probes
│   ├── wasi/
│   │   └── wasi.h                         # WASI host contract + snapshot types
│   ├── mem/
│   │   └── mem.h
│   ├── types/
│   │   └── types.h
│   ├── posix/
│   │   └── posix.h
│   ├── registry/
│   │   └── registry.h
│   ├── vartree/
│   │   └── vartree.h
│   └── util/
│       ├── bigtag.h, endian.h, fourcc.h, eightcc.h
│       ├── version.h, versiontag.h, wiretag.h
│       └── …
│
├── src/pymergetic/metal/                  # upper — portable orchestrator (kernel links)
│   ├── orchestrator/
│   │   ├── boot.c
│   │   └── loader.c
│   ├── sys/
│   │   ├── sys.c                          # in-process bootstrap; cached getters
│   │   └── hostinfo.c                     # wasm mod: hostinfo_load from /sys/pm
│   ├── mem/mem.c
│   ├── types/types.c
│   ├── posix/posix.c
│   ├── registry/registry.c
│   └── vartree/vartree.c
│
├── host/                                  # lower — platform binding + entry
│   ├── common/pymergetic/metal/
│   │   └── util/                          # bigtag.c, endian.c, …
│   │
│   ├── linux/
│   │   ├── CMakeLists.txt
│   │   ├── main.c
│   │   └── pymergetic/
│   │       └── metal/
│   │           ├── port/plat.c
│   │           ├── sys/sys.c, hostinfo.c
│   │           ├── orchestrator/wasm_host.c   # wasmtime: load mods
│   │           └── wasi/wasi_impl.c
│   │
│   ├── zephyr/
│   │   ├── CMakeLists.txt, prj.conf, Kconfig, boards/
│   │   ├── src/main.c
│   │   └── pymergetic/
│   │       └── metal/
│   │           ├── port/plat.c
│   │           ├── sys/sys.c, hostinfo.c
│   │           ├── orchestrator/wasm_host.c   # WAMR: load mods
│   │           └── wasi/zephyr_file.c, …
│   │
│   ├── rump/                              [stub] same lower layout
│   └── unikraft/                          [stub] same lower layout
│
├── mods/                                  # mod projects → .wasm
├── apps/                                  [stub] app runtimes (python, rust, …)
├── scripts/
├── docs/
├── external/                              west deps (zephyr, wamr, …)
├── west-manifest/west.yml
└── backup/1st_try/                        [ref] prior experiments — not built from
```

---

## Retired paths

| Removed | Replaced by |
|---------|-------------|
| `guest/` | `src/pymergetic/metal/` (upper, native) |
| `include/wasi/` | `include/pymergetic/metal/wasi/` |
| `include/pymergetic/mod/` | `include/pymergetic/metal/` + `PM_METAL_KERNEL_API` |
| `orchestrator/mod_host.c` | `orchestrator/wasm_host.c` (instances only) |
| `orchestrator.wasm`, `embed-orchestrator.sh` | orchestrator linked natively |

---

## Module map (read across)

| module | `include/…/metal/` | `src/…/metal/` (upper) | `host/<plat>/…/metal/` (lower) |
|--------|--------------------|-------------------------|--------------------------------|
| orchestrator | `orchestrator/*.h` | `orchestrator/boot.c`, `loader.c` | `orchestrator/wasm_host.c` |
| `sys` | `sys/*.h` | `sys/sys.c`, `sys/hostinfo.c` | `sys/sys.c`, `sys/hostinfo.c` |
| mem | `mem/mem.h` | `mem/mem.c` | — |
| types | `types/types.h` | `types/types.c` | — |
| posix | `posix/posix.h` | `posix/posix.c` | — |
| registry | `registry/registry.h` | `registry/registry.c` | — |
| vartree | `vartree/vartree.h` | `vartree/vartree.c` | — |
| port | `port/plat.h` | — | `port/plat.c`, … |
| wasi | `wasi/wasi.h` | — | `wasi/*_impl.c` |
| util | `util/*.h` | — | `host/common/…/util/*.c` |

`sys/hostinfo` and `port/` are lower-only (no upper `.c`). Upper `sys/sys.c` receives bootstrap in-process from lower — it does not read `/sys/pm` itself.

---

## Builds

| Artifact | Inputs | Defines |
|----------|--------|---------|
| **Kernel binary** (linux, zephyr, PXE image) | `src/` + `host/common/` + `host/<plat>/` | `-DPM_METAL_BUILD_KERNEL` |
| **Mod `.wasm`** | `mods/…/*.c` + `include/pymergetic/metal/` headers only | none (public API) |
| **Privileged mod `.wasm`** | same | `-DPM_METAL_BUILD_KERNEL` |

Mods never link `src/`. A privileged mod is the same wasm format — compiled with kernel-gated headers visible.

---

## Boot flow (one binary)

```
host/<plat>/main.c
  → lower: port probe, encode bootstrap
  → upper: pm_metal_orchestrator_boot(&blob)     # in-process; src/
  → lower: hostinfo publish /sys/pm              # for wasm mods only
  → lower: wasm_host_load(mod.wasm)            # on demand
```

Prior experiment code lives in `backup/1st_try/` for reference when implementing each module.
