# Source tree

Maps to [LAYERS.md](LAYERS.md). Stops at wasm interface.

---

## Two trees

| Tree | Who compiles against it |
|------|-------------------------|
| `include/pymergetic/metal/` | **mods** (guest API) |
| `src/` | **runtime binary** (everything else) |

Mods use **wasi-sdk sysroot** + `-I include/`. Start with `#include <pymergetic/metal/metal.h>`.

---

## Naming

### Files

**Rule:** `foo.h` ↔ `foo.c` same basename. A module may have **multiple** `foo.c` (common + per-plat) — linker merges; each function lives in exactly one `.c`.

**Order:** definitions in every `foo.c` follow the **same order** as declarations in `foo.h`. Skip symbols a given `.c` does not implement; do not reorder.

**Placeholders:** for each skipped symbol, leave a comment in that slot — same order, no code:

```c
/* not impl: bind — src/linux/pymergetic/metal/port/platform.c */
/* not impl: bind — src/zephyr/pymergetic/metal/port/platform.c */
```

Reason optional (`WAMR provides`, `plat-only`, …). Makes gaps grep-able and reviews obvious.

| Tree | Path |
|------|------|
| mod-facing | `include/pymergetic/metal/…/foo.h` |
| common | `src/common/pymergetic/metal/…/foo.h` · `foo.c` |
| per-plat | `src/<plat>/pymergetic/metal/…/foo.c` |
| plat-private | `src/<plat>/pymergetic/metal/…/foo.h` · `foo.c` (no common header) |

Exceptions: `main.c`, `mods/*/main.c`.

### Impl sites (per function)

Every declaration in a contract header tags where the body lives:

| Tag | Body in | Rule |
|-----|---------|------|
| `/* impl: common */` | `src/common/…/foo.c` | one copy, all targets link it |
| `/* impl: bind */` | `src/<plat>/…/foo.c` | **every** built target has an impl |
| `/* impl: zephyr */` | `src/zephyr/…/foo.c` | that target only (plat-private header) |

One header can mix tags. Example `platform.h`: some calls OS-neutral → `common`; probes → `bind` on each plat.

```c
/* src/common/pymergetic/metal/port/platform.h */

/* impl: common */
pm_metal_port_target_id_t pm_metal_port_target_id(void);

/* impl: bind */
uint64_t pm_metal_port_machine_ram(void);
```

Symmetric naming lets you find `platform.c` in common and/or `src/linux/`, `src/zephyr/` and know which file owns which symbol.

### Symbols

```
pymergetic/metal/<module>/…/<stem>.h  →  pm_metal_<module>_…_<stem>_
```

- omit `<stem>` when it repeats the last dir (`runtime/runtime.h` → `pm_metal_runtime_`)
- sole contract in a module dir (`port/platform.h`) → `pm_metal_port_`

| Header | Prefix | Example |
|--------|--------|---------|
| `metal/metal.h` | — | umbrella only |
| `port/platform.h` | `pm_metal_port_` | `pm_metal_port_machine_ram()` |
| `runtime/runtime.h` | `pm_metal_runtime_` | `pm_metal_runtime_run_wasm()` |

Private `src/<plat>/` symbols: `static` or plat-local.

---

## Tree

```
packages/metal/
│
├── include/pymergetic/metal/
│   └── metal.h
│
├── src/
│   ├── common/pymergetic/metal/   # cross-target — runtime + contracts
│   │   ├── port/platform.h        # OS floor API (impl in src/<plat>/)
│   │   ├── runtime/
│   │   │   ├── runtime.h
│   │   │   └── runtime.c
│   │   └── util/
│   │
│   ├── linux/
│   │   ├── CMakeLists.txt
│   │   ├── main.c
│   │   └── pymergetic/metal/port/platform.c
│   │   # wasi: WAMR linux platform
│   │
│   ├── zephyr/
│   │   ├── CMakeLists.txt, Kconfig, prj.conf, boards/
│   │   ├── main.c
│   │   └── pymergetic/metal/
│   │       ├── port/platform.c
│   │       └── wasi/              # private
│   │           ├── file.h
│   │           └── file.c
│   │
│   ├── rump/                      # [stub]
│   └── unikraft/                  # [stub]
│
├── mods/
│   ├── t0_hello/main.c
│   └── t1_read/main.c
│
├── build/                         # gitignored
│   ├── linux/runtime/
│   ├── zephyr/{native_sim,native_sim_mod,qemu_x86_64,qemu_x86_64_mod}/
│   ├── mods/
│   └── ide/
│
├── scripts/
├── docs/
├── external/                      # west — gitignored, vanilla only
├── west-manifest/
└── backup/
```

---

## Header ↔ .c map

| Module | Header | `.c` (one or more) |
|--------|--------|---------------------|
| `runtime` | `src/common/…/runtime.h` | `src/common/…/runtime.c` |
| `platform` | `src/common/…/platform.h` | `src/common/…/platform.c`? + `src/<plat>/…/platform.c` — per `impl:` tags |
| `wasi/file` | `src/zephyr/…/file.h` | `src/zephyr/…/file.c` — all `impl: zephyr` |

---

## Common vs per-target

| Path | What |
|------|------|
| `src/common/pymergetic/metal/` | contract `.h` + any `impl: common` `.c` |
| `src/<plat>/pymergetic/metal/` | `impl: bind` + plat-private modules |
| `src/<plat>/main.c` | entry |

Per-function `impl:` tags in each header are authoritative — not the directory alone.

---

## Path → layer

| Path | Layer |
|------|-------|
| `include/…/` | mod contract |
| `src/common/…/port/platform.h` | port contract |
| `src/<plat>/…/port/platform.c` | port impl |
| `src/common/…/runtime/` | runtime + wamr |
| `src/<plat>/…/` (private) | plat-only (wasi shim, …) |
| `src/<plat>/main.c` | entry |
| `mods/` + wasi-sdk | wasm guests |

---

## Rules

| Rule | |
|------|--|
| `include/` = mod-facing only | runtime must not include from here |
| Every contract function | `/* impl: common */`, `/* impl: bind */`, or `/* impl: <plat> */` |
| `.c` function order | matches `.h` declaration order |
| Skipped symbols in `.c` | `/* not impl: <tag> — <path> */` placeholder, same slot |
| `src/common/` | contract `.h` + `impl: common` `.c` |
| `src/<plat>/` | `impl: bind` + plat-private; OS `#include`s only here |
| Public symbols | `pm_metal_<module_path>_` |
| `external/` + `.tools/` | **vanilla** — west/toolchain pins only; no patches in-tree |
| Artifacts | `build/` — gitignored |

Adapt WAMR, Zephyr, wasi-sdk, etc. from `src/` (CMake flags, shims, wrappers) — never edit vendored trees. Fork/patch upstream only if unavoidable; then bump pin in `west-manifest/` and document why.

---

## Build outputs

| Path | Output |
|------|--------|
| `build/linux/runtime/` | `pm-linux-runtime` |
| `build/zephyr/<profile>/` | `zephyr.elf` / `zephyr.exe` |
| `build/mods/` | `*.wasm` |
| `build/ide/` | merged `compile_commands.json` |

Also gitignored: `.tools/`, `external/`, `.cache/`, `.venv/`.

---

## Builds

| Artifact | Inputs | Output |
|----------|--------|--------|
| **runtime binary** | `src/common/pymergetic/metal/` + `src/<plat>/` + WAMR | `build/<plat>/…` |
| **mod `.wasm`** | `mods/` + wasi-sdk + `-I include/` | `build/mods/` |

---

## Omit (now)

```
apps/
orchestrator/ mem/ types/ …        # mod API in include/ — later
```
