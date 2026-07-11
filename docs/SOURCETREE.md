# Source tree

Maps to [LAYERS.md](LAYERS.md). Stops at wasm interface.

---

## Two trees (plus one narrow exception)

| Tree | Who compiles against it |
|------|-------------------------|
| `include/pymergetic/metal/` | **mods** (guest API) |
| `src/` | **runtime binary** (everything else) |

Mods use **wasi-sdk sysroot** + `-I include/`. Start with `#include <pymergetic/metal/metal.h>`.

**Exception — `shared`:** a handful of leaf, zero-OS-dependency utilities (e.g. `util/size.h`) are genuinely useful on both sides. For these only, `include/…/foo.h` is the contract (either side may include it) and `include/…/foo_impl.h` holds the real bodies. `foo_impl.h` is never included directly by callers — exactly one thin loader `.c` per *binary* pulls it in: `src/shared/pymergetic/metal/…/foo.c` for the runtime today, and (later) a modlib loader for guests. This keeps the logic written once (DRY) while still letting each binary — native runtime, wasm guest — compile its own object code, which is unavoidable since they're different targets; that duplication is cheap for small leaf utilities. Everything else in `include/` remains **mod-facing only** — the runtime must not include from there.

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
| `/* impl: shared */` | `include/…/foo_impl.h`, via `src/shared/…/foo.c` (+ later a modlib loader) | leaf util, no OS dependency — usable by mods too |

One header can mix tags. Example `platform.h`: some calls OS-neutral → `common`; probes → `bind` on each plat.

```c
/* src/common/pymergetic/metal/port/platform.h */

/* impl: common */
pm_metal_port_target_id_t pm_metal_port_target_id(void);

/* impl: bind */
int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len);
```

Symmetric naming lets you find `platform.c` in common and/or `src/linux/`, `src/zephyr/` and know which file owns which symbol.

**Ops-struct flavor of `bind`:** `pymergetic/metal/memory/` (see Tree below) groups closely-related `bind` functions into one struct-of-function-pointers per module instead of tagging each function separately. All three memory modules (`ram`, `kheap`, `bytecode`) share **one struct layout**, `pm_metal_memory_ops_t` in `memory/ops.h` — that header holds *only* the struct definition, nothing else. Each module then gets its own contract header declaring its own `bind` getter that returns a pointer to that shared struct type, with only the slots it uses filled in (the rest `NULL`):

```c
/* src/common/pymergetic/metal/memory/ops.h — the one shared layout,
 * plus a kind enum + resolve() for dynamic lookup (see below) */

typedef enum pm_metal_memory_kind {
	PM_METAL_MEMORY_RAM = 0,
	PM_METAL_MEMORY_KHEAP,
	PM_METAL_MEMORY_BYTECODE,
	PM_METAL_MEMORY_KIND_COUNT,
} pm_metal_memory_kind_t;

typedef struct pm_metal_memory_ops {
	uint64_t (*probe)(void);
	void *(*establish)(uint64_t requested_bytes, uint64_t *out_bytes);
	void (*release)(void);
	uint64_t (*bytes)(void);
	void *(*alloc)(uint32_t size);
	void (*free)(void *ptr);
} pm_metal_memory_ops_t;

/* impl: common — src/common/pymergetic/metal/memory/ops.c */
const pm_metal_memory_ops_t *pm_metal_memory_resolve(pm_metal_memory_kind_t kind);
```

```c
/* src/common/pymergetic/metal/memory/kheap.h — one getter, this module's contract */

#include "pymergetic/metal/memory/ops.h"

/* impl: bind — src/linux/…/memory/kheap.c
 *              src/zephyr/…/memory/kheap.c
 *
 * ->establish()/->release()/->bytes() are set; ->probe()/->alloc()/->free()
 * are NULL — this kind has no probe and is never sub-allocated. */
const pm_metal_memory_ops_t *pm_metal_memory_kheap_ops(void);
```

Each target's `.c` (one per module — `memory/kheap.c`, not a shared per-target `memory/ops.c`) defines one `static const` ops table (function pointers to `static` functions in that same file, `NULL` for the slots this module doesn't use) and the getter just returns its address — bound at build/link time like any other `bind` symbol, so there is no runtime registration step and the returned pointer is valid for the whole process lifetime. Callers do `pm_metal_memory_kheap_ops()->establish(...)`. `NULL` here always means "this module doesn't have this operation" (e.g. `ram` has no `alloc`) — a slot a module *does* use but a target hasn't implemented yet (e.g. zephyr's `kheap`/`bytecode` today) still gets a real stub function that returns `0`/`NULL` at call time, never a `NULL` field, so callers never need to null-check before calling a slot their module is documented to support. Use this flavor when a handful of functions are always used together and always come from the same target implementation (so grouping them behind one lookup is more useful than N separate symbols); use plain per-function `bind` (like `read_file` above) when a symbol stands alone.

`pm_metal_memory_ops.h`'s `pm_metal_memory_resolve(kind)` is a companion lookup for callers that pick a kind dynamically (e.g. a diagnostics loop over all three) instead of calling a dedicated getter at a call site that already knows its kind at compile time. It has exactly one implementation, `src/common/pymergetic/metal/memory/ops.c` (`impl: common`, not per-target) — it just `switch`es on `kind` and forwards to `pm_metal_memory_ram_ops()`/`kheap_ops()`/`bytecode_ops()`, so it carries no target-specific logic of its own.

### Symbols

```
pymergetic/metal/<module>/…/<stem>.h  →  pm_metal_<module>_…_<stem>_
```

- omit `<stem>` when it repeats the last dir (`runtime/runtime.h` → `pm_metal_runtime_`)
- sole contract in a module dir (`port/platform.h`) → `pm_metal_port_`

| Header | Prefix | Example |
|--------|--------|---------|
| `metal/metal.h` | — | umbrella only |
| `port/platform.h` | `pm_metal_port_` | `pm_metal_port_read_file()` |
| `runtime/runtime.h` | `pm_metal_runtime_` | `pm_metal_runtime_run_wasm()` |
| `memory/kheap.h` | `pm_metal_memory_kheap_` | `pm_metal_memory_kheap_ops()->establish()` |

Private `src/<plat>/` symbols: `static` or plat-local.

---

## Tree

```
packages/metal/
│
├── include/pymergetic/metal/
│   ├── metal.h
│   └── util/
│       ├── size.h                 # contract — mods and runtime may include
│       ├── size_impl.h            # body — only a shared/ loader includes this
│       ├── arena.h                # contract — mods and runtime may include
│       └── arena_impl.h           # body — only a shared/ loader includes this
│
├── src/
│   ├── common/pymergetic/metal/   # cross-target — runtime + contracts
│   │   ├── port/platform.h        # OS floor API (impl in src/<plat>/)
│   │   ├── port/lock.h            # one mutex primitive (impl in src/<plat>/) — see docs/RUNTIME.md "Concurrency"
│   │   ├── memory/                # ops-struct contracts (impl in src/<plat>/)
│   │   │   ├── memory.h           # convenience umbrella — re-exports the 4 below
│   │   │   ├── ops.h              # shared struct layout + kind enum + resolve()
│   │   │   ├── ops.c              # resolve() impl — impl: common, dispatches only
│   │   │   ├── ram.h              # machine RAM probe
│   │   │   ├── kheap.h            # WAMR pool (wasm linear mem + WAMR structs)
│   │   │   └── bytecode.h         # mod bytecode arena — separate from kheap
│   │   └── runtime/
│   │       ├── runtime.h
│   │       └── runtime.c
│   │
│   ├── shared/pymergetic/metal/   # thin loaders only — real body in include/…_impl.h
│   │   └── util/
│   │       ├── size.c
│   │       └── arena.c            # backs memory/bytecode.c's arena
│   │
│   ├── linux/
│   │   ├── CMakeLists.txt
│   │   ├── main.c
│   │   ├── thread_stress_test.c   # pm-linux-thread-stress — EXCLUDE_FROM_ALL, see scripts/verify-linux-threads.sh
│   │   └── pymergetic/metal/
│   │       ├── port/{platform,lock}.c
│   │       └── memory/{ram,kheap,bytecode}.c
│   │   # wasi: WAMR linux platform
│   │
│   ├── zephyr/
│   │   ├── CMakeLists.txt, Kconfig, prj.conf, boards/
│   │   ├── main.c
│   │   └── pymergetic/metal/
│   │       ├── port/{platform,lock}.c
│   │       ├── memory/{ram,kheap,bytecode}.c
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
| `port/lock` | `src/common/…/port/lock.h` | `src/<plat>/…/port/lock.c` — `bind`, one mutex primitive per target |
| `memory/ops` | `src/common/…/memory/ops.h` | `src/common/…/memory/ops.c` — `impl: common`, `resolve()` only, no per-target impl |
| `memory/ram` | `src/common/…/memory/ram.h` | `src/<plat>/…/memory/ram.c` — ops-struct `bind`, one getter per target |
| `memory/kheap` | `src/common/…/memory/kheap.h` | `src/<plat>/…/memory/kheap.c` — ops-struct `bind`, one getter per target |
| `memory/bytecode` | `src/common/…/memory/bytecode.h` | `src/<plat>/…/memory/bytecode.c` — ops-struct `bind`, one getter per target |
| `wasi/file` | `src/zephyr/…/file.h` | `src/zephyr/…/file.c` — all `impl: zephyr` |
| `util/size` | `include/…/size.h` (+ body in `size_impl.h`) | `src/shared/…/size.c` (loader) — all `impl: shared` |
| `util/arena` | `include/…/arena.h` (+ body in `arena_impl.h`) | `src/shared/…/arena.c` (loader) — all `impl: shared`; backs `memory/bytecode.c`'s arena |

---

## Common vs per-target

| Path | What |
|------|------|
| `src/common/pymergetic/metal/` | contract `.h` + any `impl: common` `.c` |
| `src/shared/pymergetic/metal/` | thin `impl: shared` loaders only — real body in `include/…_impl.h` |
| `src/<plat>/pymergetic/metal/` | `impl: bind` + plat-private modules |
| `src/<plat>/main.c` | entry |

Per-function `impl:` tags in each header are authoritative — not the directory alone.

---

## Path → layer

| Path | Layer |
|------|-------|
| `include/…/` | mod contract (+ `util/` leaf-util contract shared with the runtime) |
| `src/common/…/port/platform.h` | port contract |
| `src/<plat>/…/port/platform.c` | port impl |
| `src/common/…/memory/*.h` | memory contracts (ops-struct `bind`) |
| `src/<plat>/…/memory/*.c` | memory impl — one ops table per module per target |
| `src/common/…/runtime/` | runtime + wamr |
| `src/shared/…/` | leaf-util loaders — body in `include/…_impl.h` |
| `src/<plat>/…/` (private) | plat-only (wasi shim, …) |
| `src/<plat>/main.c` | entry |
| `mods/` + wasi-sdk | wasm guests |

---

## Rules

| Rule | |
|------|--|
| `include/` = mod-facing only, **except** `impl: shared` leaf utils (`util/`, zero OS dependency) | runtime must not otherwise include from here |
| `include/…_impl.h` | never included by ordinary callers — only by its one `src/shared/…` loader `.c` (+ later a modlib loader) |
| Every contract function | `/* impl: common */`, `/* impl: bind */`, `/* impl: <plat> */`, or `/* impl: shared */` |
| `.c` function order | matches `.h` declaration order |
| Skipped symbols in `.c` | `/* not impl: <tag> — <path> */` placeholder, same slot |
| `src/common/` | contract `.h` + `impl: common` `.c` |
| `src/shared/` | thin `impl: shared` loader `.c` only — no logic, no OS `#include`s |
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
