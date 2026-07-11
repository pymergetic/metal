# Platform layers

How pymergetic-metal splits **contract**, **policy**, and **mechanism** so the same upper runs on Linux (dev) and Zephyr (ship) without accidental OS-only APIs leaking upward.

See also: [LAYERS.md](LAYERS.md) (runtime / upper / lower / mod) · [SOURCETREE.md](SOURCETREE.md) · [MEMORY_BASELINE.md](MEMORY_BASELINE.md).

---

## One rule

> **Metal decides meaning. Port defines the portable floor. Port impls lie about the OS so metal does not have to.**

`port/` is a **metal module** — lower-only. Contract in `include/pymergetic/metal/port/`; per-target impl in `host/<plat>/pymergetic/metal/port/`. `wasi/` impl lives in lower `host/<plat>/pymergetic/metal/wasi/`.

Dependency direction (strict):

```
host/<plat>/main.c                 # lower entry
  → src/pymergetic/metal/*         upper — orchestrator policy (no OS headers)
    → metal/port/plat.h            contract — probe floor
      → host/<plat>/metal/port/*   mechanism — ONLY layer that may include OS headers
```

**Forbidden:** `#include <zephyr/...>`, `<unistd.h>`, `<pthread.h>`, `<linux/...>` anywhere under `include/pymergetic/metal/`, `src/pymergetic/metal/`, or shared upper sources.

**Allowed in lower only:** OS headers, Kconfig (`CONFIG_*`), devicetree, board traits.

**Include tree:** everything under `include/pymergetic/metal/` only. See [Access levels](#access-levels).

---

## Access levels

Not a runtime security boundary — **compile-time API surface** per build personality.

| Build | Defines | Sees |
|-------|---------|------|
| Mod `.wasm` | (none) | public metal API (always emitted) |
| Privileged mod `.wasm` | `-DPM_METAL_BUILD_KERNEL` | public + kernel-gated API |
| Kernel binary | `-DPM_METAL_BUILD_KERNEL` | full API |

Public symbols need **no extra define**. Kernel-only symbols use `PM_METAL_KERNEL_API` in `metal/export.h` and disappear unless `PM_METAL_BUILD_KERNEL` is set (`metal/build.h`).

Optional debug tier — `PM_METAL_VIS_DEBUG` in `metal/export.h`:

| Level | Value | Build | Sees |
|-------|-------|-------|------|
| `PM_METAL_VIS_RUNTIME` | 1 | default | runtime API |
| `PM_METAL_VIS_DEBUG` | 2 | `-DPM_METAL_MAX_VIS=2` | runtime + debug-tagged API |

```c
#include <pymergetic/metal/export.h>

PM_METAL_API(int, pm_metal_mem_alloc, (size_t n))
PM_METAL_KERNEL_API(int, pm_metal_orchestrator_boot, (const pm_metal_sys_bootstrap_t *blob))
```

There is no separate mod include tree. Privileged mods are the same wasm format — built with `-DPM_METAL_BUILD_KERNEL`.

---

## Four truths (do not mix)

| Truth | Question | Location |
|-------|----------|----------|
| **Contract** | What may metal code assume on every target? | `include/pymergetic/metal/` |
| **Policy (upper)** | How does orchestrator boot, lay out RAM, load mods? | `src/pymergetic/metal/` |
| **Mechanism (lower)** | How does this OS probe RAM / host WASI / load wasm? | `host/<plat>/pymergetic/metal/` |
| **Wasm guest** | What does a mod compile against? | `include/pymergetic/metal/` headers only |

---

## Directory map

```
include/pymergetic/metal/          contract — mods use headers only
├── build.h, export.h
├── orchestrator/, sys/, mem/, types/, posix/, registry/, vartree/
├── port/plat.h, wasi/wasi.h
└── util/

src/pymergetic/metal/              upper — kernel links; mods never link
├── orchestrator/boot.c, loader.c
├── sys/sys.c, mem/mem.c, …
└── …

host/<plat>/                       lower — per target
├── main.c (or src/main.c)
└── pymergetic/metal/
    ├── port/plat.c
    ├── sys/sys.c, hostinfo.c
    ├── orchestrator/wasm_host.c
    └── wasi/wasi_impl.c

mods/, apps/                       mod sources → .wasm
```

---

## Layer responsibilities

### `metal/port/plat.h` — portable floor (contract)

Smallest intersection of Linux and Zephyr capabilities. Add a function here only if **both** ports can implement it (or it sits behind an explicit `PM_METAL_PORT_HAS_*` capability in `metal/port/config.h`).

Planned surface (names per [NAMING.md](NAMING.md)):

```c
/* --- probes (layout inputs) --- */
uint64_t pm_metal_port_machine_ram(void);
uint64_t pm_metal_port_link_used(void);
uintptr_t pm_metal_port_ram_base(void);

/* --- backing store (heaps, mod arenas) --- */
void *pm_metal_port_map(size_t size, unsigned prot);
void  pm_metal_port_unmap(void *addr, size_t size);
void  pm_metal_port_bzero(void *addr, size_t size);

/* --- heap buckets (not raw libc in metal) --- */
typedef enum {
  PM_METAL_PORT_HEAP_KERNEL,
  PM_METAL_PORT_HEAP_MALLOC,
  PM_METAL_PORT_HEAP_MOD,
} pm_metal_port_heap_t;

void *pm_metal_port_alloc(size_t n, pm_metal_port_heap_t heap);
void  pm_metal_port_free(void *p, pm_metal_port_heap_t heap);

/* --- instance / mod runtime --- */
void *pm_metal_port_tls_get(unsigned slot);
int   pm_metal_port_vfs_read(const char *path, void *buf, size_t cap, size_t *out);
```

Upper policy code includes `metal/port/plat.h` only — never OS headers. Lower `sys/sys.c` encodes probes; it must not include OS headers except via `port/`.

### `src/` — product semantics (upper / policy)

- **Memory layout slots:** machine, kernel static, k_pool, malloc — **`orchestrator/boot` + `sys` + `mem`**
- **Boot report** — `orchestrator/boot.c`
- **Mod loader** — `orchestrator/loader.c` drives lower `wasm_host`
- **Process model:** FRESH vs PERSIST handles (`orchestrator/instance.h`)

**Upper** owns layout policy: `orchestrator/boot` receives bootstrap **in-process** from lower (struct pointer), sizes arena via `mem`.

**Lower** `sys/sys.c` reads `port/` probes and encodes exchange records; `sys/hostinfo.c` publishes bootstrap for **wasm mods** (POSIX on linux; Zephyr VFS on firmware).

```c
/* orchestrator/boot — upper, native */
uint64_t machine = pm_metal_sys_machine_ram();
```

### `host/<plat>/metal/port/*.c` — adaptation (thick on purpose, lower only)

| Concern | Linux | Zephyr |
|---------|-------|--------|
| Machine RAM | config / env probe | E820 sum or DT fallback |
| Link used | `_end - ram_base` | same |
| Kernel heap | mapped region + allocator | `k_malloc` pool |
| Userspace blob | TLSF pool (malloc + mmap) | flat SRAM tail; MMU map on real x86 |
| Map/unmap | `mmap`/`munmap` → TLSF tail | `--wrap=mmap` / `--wrap=munmap` |
| TLS | `pthread` keys | Zephyr TLS / thread local |
| VFS | lower `open/read` | embedded image / littlefs later |
| Wasm loader | wasmtime via `wasm_host.c` | WAMR via `wasm_host.c` |
| Traits | (none) | fake vs real metal |

**Fake metal is not a third OS.** It is the Zephyr port with traits (`PM_ZEPHYR_FAKE_METAL`): in-link arena, DT window cap — same upper code, different `port/plat.c` branch. Linux always uses host mmap; no fake twin.

Transitional reference for probe/backing ideas: `backup/1st_try/` — not built from.

---

## WASI boundary

WASI is the **binary format and runtime syscall surface** for wasm mods — not for the orchestrator upper.

| Side | Role |
|------|------|
| **Upper** | Native C above WASI; hosts guests |
| **Lower `wasi/`** | Implements WASI for guests — posix, vfs, clocks, env (foundation for python/rust later) |
| **Lower `wasm_host/`** | Load/instantiate/link mod `.wasm` |
| **Mod** | `wasm32-wasip1` (or component model later); compiles with `include/pymergetic/metal/` headers only |

---

## Memory: slots vs backings

| Metal slot (policy — upper `orchestrator/boot`) | Where values come from |
|-------------------------------------------------|------------------------|
| `machine_ram` | upper `sys` ← lower encode ← `pm_metal_port_machine_ram()` |
| `kernel_link` | upper `sys` ← lower encode ← `pm_metal_port_link_used()` |
| `kernel_static` | arithmetic in `orchestrator/boot` |
| `kernel_heap` | upper `sys` exchange record |
| `userspace_blob` | upper `mem` sized from `sys` `arena_budget` |
| `mod_heap` (future) | upper `mem` / component shared blob |

Upper owns slot math and boot report. Lower `port/` owns raw probes only.

---

## Resemble Linux on Zephyr vs minimal interface

These apply at **different layers**:

| Goal | Where | How |
|------|-------|-----|
| Minimal, Zephyr-safe API | `metal/port/plat.h` | Small contract; new entries need dual-port proof |
| Fast dev loop | `host/linux` + `src/` upper | Primary loop for mod loader work |
| Zephyr CI without HW | `host/zephyr` + fake metal trait | Same upper + loader path; port emulates constraints |
| Same behavior | `src/` + shared smoke tests | One boot report shape, one loader, slice verify |

Do **not** widen the metal API to “feel like Linux.” Emulate Linux-like behavior **inside** lower `port/plat.c` when fake metal requires it.

---

## Capability flags

Metal and port contract code must not use Zephyr `CONFIG_*` directly. Lower port impls translate Kconfig / CMake into compile-time caps:

```c
/* include/pymergetic/metal/port/config.h — values set by port build */
#define PM_METAL_PORT_HAS_MMAP       1
#define PM_METAL_PORT_HAS_TLS        1
#define PM_METAL_PORT_HAS_VFS_EMBED  0   /* zephyr ship path when ready */
```

Check `PM_METAL_PORT_HAS_*` in shared code. Set flags in lower build glue or generated headers.

---

## Guardrails

1. **Twin compile** — `host/linux` and `host/zephyr` link the same `src/` upper; only lower `port/` differs per target.
2. **Include lint (CI)** — fail if OS headers appear outside `host/<plat>/pymergetic/metal/`.
3. **New `plat.h` checklist** — Linux ok? Zephyr real metal ok? Fake metal ok enough for CI? Any no → keep helper private in lower `port/`.
4. **Mod boundary** — mods compile headers only; privileged mods add `-DPM_METAL_BUILD_KERNEL`.

---

## First vertical slice

Prove runtime boot before full metal stack:

1. `include/pymergetic/metal/sys/sys.h` — bootstrap exchange format.
2. `include/pymergetic/metal/port/plat.h` — probe trio (`machine_ram`, `link_used`, `arena_budget`).
3. Lower: symmetric `sys/sys.c` + `sys/hostinfo.c` per host tree (`host/linux/…`, `host/zephyr/…`).
4. Upper: `sys/sys.c` + `orchestrator/boot.c` — in-process bootstrap, print `machine_ram`, `ready`.
5. CI: same upper sources on both targets; stdout matches.

Then extend exchange types and `mem` one piece at a time. Mod loading via `wasm_host` follows.
