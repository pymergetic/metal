# Platform layers

How pymergetic-metal splits **contract**, **policy**, and **mechanism** so the same core runs on Linux (dev) and Zephyr (ship) without accidental OS-only APIs leaking upward.

See also: [LAYERS.md](LAYERS.md) (naming: engine / orchestrator / instance) · [SOURCETREE.md](SOURCETREE.md) · [MEMORY_BASELINE.md](MEMORY_BASELINE.md).

---

## One rule

> **Metal decides meaning. Port defines the portable floor. Port impls lie about the OS so metal does not have to.**

`port/` is a **metal module** — engine-only, but same symmetry row as `pm_mem`, `pm_sys`, etc. Contract in `include/pymergetic/metal/port/`; per-target impl in `host/<plat>/pymergetic/metal/port/`. `pm_hostinfo.c` and `wasi/` sit outside `metal/` as engine siblings.

Dependency direction (strict):

```
host/<plat>/main.c                 # engine entry
  → pymergetic/metal/*             orchestrator metal on engine side (pm_sys encode, mod_host, …)
    → metal/port/plat.h            contract — probe, map, alloc (no OS headers)
      → metal/port/plat.c, …         mechanism — ONLY layer that may include OS headers
```

**Forbidden:** `#include <zephyr/...>`, `<unistd.h>`, `<pthread.h>`, `<linux/...>` anywhere under `include/pymergetic/`, `guest/` (orchestrator), or shared orchestrator metal sources.

**Allowed in port impl only:** OS headers, Kconfig (`CONFIG_*`), devicetree, board traits.

**Include tree:** `include/pymergetic/` is the engine + orchestrator catalog. **Mod instances** use a separate include path (`include/pymergetic/mod/` only). See [Access levels](#access-levels).

---

## Access levels

Not a security boundary — **minimal API surface** per build personality.

### Target (WASI) — two compile profiles

| Build | Includes | Kernel contact |
|-------|----------|----------------|
| **Orchestrator** | `include/pymergetic/**` | `pm_sys` API — one `fd_read` of `/sys/pm` at boot, then cached getters |
| **Mod instance** | `include/pymergetic/mod/` (+ WIT) only | stock WASI; orchestrator loads via component link |

Wasm mod instances do **not** use `PM_VIS_MOD`. Boundary = **what headers you link**, not macro stripping.

`pm_vis.h` stays for **orchestrator + engine** only — gates `PM_VIS_RUNTIME` vs `PM_VIS_DEBUG`:

| Level | Value | Build | Sees |
|-------|-------|-------|------|
| `PM_VIS_RUNTIME` | 1 | orchestrator, engine `host/<plat>/` | `pm_sys`, `pm_mem`, orchestrator, … |
| `PM_VIS_DEBUG` | 2 | optional `-DPM_MAX_VIS=2` | runtime + debug-tagged API |

```c
#include <pymergetic/pm_vis.h>

PM_API(PM_VIS_RUNTIME, int, pm_metal_memory_boot, (void))
```

Header: `include/pymergetic/pm_vis.h`. Orchestrator + engine default: `PM_MAX_VIS=1`.

---

## Three truths (do not mix)

| Truth | Question | Location |
|-------|----------|----------|
| **Contract** | What may metal code assume on every engine target? | `include/pymergetic/metal/port/plat.h` |
| **Policy** | How does orchestrator boot, lay out RAM, load instances? | `include/pymergetic/metal/` + `guest/pymergetic/metal/` |
| **Mechanism** | How does this OS probe RAM / mmap / TLS? | `host/<plat>/pymergetic/metal/port/` |
| **Instance import** | What may mod instances call? | `mod/pm_mod.h` + WIT |

---

## Directory map

### Target

```
include/pymergetic/              engine + orchestrator catalog
├── pm_vis.h                     RUNTIME vs DEBUG (orchestrator + engine only)
├── mod/                         mod instance SDK
└── metal/                       shared metal contract
    ├── orchestrator/            boot, loader, instance headers
    ├── port/plat.h              probe contract (engine impl only)
    └── pm_mem.h, pm_sys.h, …

host/<plat>/                       engine — per target (linux, zephyr, rump, unikraft)
├── main.c, CMakeLists.txt, …
└── pymergetic/
    ├── metal/
    │   ├── orchestrator/mod_host.c
    │   ├── pm_mem.c, pm_sys.c, …
    │   └── port/                  plat.c, efi_ram.c, …
    ├── wasi/wasi_impl.c
    └── pm_hostinfo.c              publish bootstrap blob → /sys/pm

guest/pymergetic/metal/            orchestrator — portable wasm32-wasip1 (no port/)

mods/, apps/                       instance sources → wasm
```

---

## Layer responsibilities

### `metal/port/plat.h` — portable floor (contract)

Smallest intersection of Linux and Zephyr capabilities. Add a function here only if **both** ports can implement it (or it sits behind an explicit `PM_PLAT_HAS_*` capability in `metal/port/config.h`).

Planned surface:

```c
/* --- probes (layout inputs) --- */
size_t   pm_plat_machine_ram(void);
size_t   pm_plat_link_used(void);
uintptr_t pm_plat_ram_base(void);

/* --- backing store (heaps, mod arenas) --- */
void *pm_plat_map(size_t size, unsigned prot);
void  pm_plat_unmap(void *addr, size_t size);
void  pm_plat_bzero(void *addr, size_t size);

/* --- heap buckets (not raw libc in metal) --- */
typedef enum {
  PM_PLAT_HEAP_KERNEL,
  PM_PLAT_HEAP_MALLOC,
  PM_PLAT_HEAP_MOD,
} pm_plat_heap_t;

void *pm_plat_alloc(size_t n, pm_plat_heap_t heap);
void  pm_plat_free(void *p, pm_plat_heap_t heap);

/* --- instance / mod runtime --- */
void *pm_plat_tls_get(unsigned slot);
int   pm_plat_vfs_read(const char *path, void *buf, size_t cap, size_t *out);
```

Policy code under orchestrator `metal/` includes `metal/port/plat.h` only — never OS headers. Engine `metal/` `.c` files encode probes; they must not include OS headers either except via `port/`.

### `metal/` — product semantics (policy)

- **Memory layout slots:** machine, kernel static, k_pool, malloc — **`orchestrator/boot` + `pm_sys` + `pm_mem`**
- **Boot report** — orchestrator `orchestrator/boot`
- **Instance loader** — wasm component link; loads **mods** and **apps**
- **Process model:** FRESH vs PERSIST instance handles (`orchestrator/instance.h`)

**Orchestrator** owns layout policy: `orchestrator/boot` reads cached `pm_sys` (one `/sys/pm` read at init), sizes arena via `pm_mem`.

**Engine** `pm_sys` reads `port/` probes and encodes exchange records; `pm_hostinfo` publishes to `/sys/pm` before orchestrator wasm starts.

```c
/* orchestrator/boot */
size_t machine = pm_sys_machine_ram();
```

### `metal/port/*.c` — adaptation (thick on purpose, engine only)

| Concern | Linux | Zephyr |
|---------|-------|--------|
| Machine RAM | config / env probe | E820 sum or DT fallback |
| Link used | `_end - ram_base` | same |
| Kernel heap | mapped region + allocator | `k_malloc` pool |
| Userspace blob | TLSF pool (malloc + mmap) | flat SRAM tail; MMU map on real x86 |
| Map/unmap | `mmap`/`munmap` → TLSF tail | `--wrap=mmap` / `--wrap=munmap` |
| TLS | `pthread` keys | Zephyr TLS / thread local |
| VFS | engine `open/read` | embedded image / littlefs later |
| Traits | (none) | fake vs real metal |

**Fake metal is not a third OS.** It is the Zephyr port with traits (`PM_ZEPHYR_FAKE_METAL`): in-link arena, DT window cap — same metal code, different `metal/port/plat.c` branch. Linux always uses host mmap; no fake twin.

Transitional reference for probe/backing ideas: `backup/1st_try/` — not built from.

---

## Memory: slots vs backings

| Metal slot (policy — orchestrator `orchestrator/boot`) | Where values come from |
|--------------------------------------------------------|------------------------|
| `machine_ram` | orchestrator `pm_sys` ← engine `pm_sys` ← port probe |
| `kernel_link` | orchestrator `pm_sys` ← engine `pm_sys` ← `pm_plat_link_used()` |
| `kernel_static` | arithmetic in `orchestrator/boot` |
| `kernel_heap` | orchestrator `pm_sys` exchange record |
| `userspace_blob` | orchestrator `pm_mem` sized from `pm_sys` `arena_budget` |
| `mod_heap` (future) | orchestrator `pm_mem` / component shared blob |

Orchestrator owns slot math and boot report. Engine `port/` owns raw probes only.

---

## Resemble Linux on Zephyr vs minimal interface

These apply at **different layers**:

| Goal | Where | How |
|------|-------|-----|
| Minimal, Zephyr-safe API | `metal/port/plat.h` | Small contract; new entries need dual-port proof |
| Fast dev loop | `host/linux` engine + `metal/port/` | Primary loop for instance loader work |
| Zephyr CI without HW | `host/zephyr` + fake metal trait | Same metal + loader path; port emulates constraints |
| Same behavior | `metal/` + shared smoke tests | One boot report shape, one loader, twin verify |

Do **not** widen the metal API to “feel like Linux.” Emulate Linux-like behavior **inside** `metal/port/plat.c` when fake metal requires it.

---

## Capability flags

Metal and port contract code must not use Zephyr `CONFIG_*` directly. Port impls translate Kconfig / CMake into compile-time caps:

```c
/* include/pymergetic/metal/port/config.h — values set by port build */
#define PM_PLAT_HAS_MMAP       1
#define PM_PLAT_HAS_TLS        1
#define PM_PLAT_HAS_VFS_EMBED  0   /* zephyr ship path when ready */
```

Check `PM_PLAT_HAS_*` in shared code. Set flags in `metal/port/plat.c` build glue or generated headers (`host/linux/config/pm_config.h`, Zephyr `autoconf` shim).

---

## Guardrails

1. **Twin compile** — `host/linux` and `host/zephyr` engines run the same `orchestrator.wasm`; only `metal/port/` differs per target.
2. **Include lint (CI)** — fail if OS headers appear outside `host/<plat>/pymergetic/metal/port/`.
3. **New `plat.h` checklist** — Linux ok? Zephyr real metal ok? Fake metal ok enough for CI? Any no → keep helper private in `metal/port/`.
4. **Instance boundary** — mod instances: `mod/` + WIT only. App instances: separate SDK later.

---

## First vertical slice

Prove engine → orchestrator handoff before full metal stack:

1. `include/pymergetic/metal/pm_sys.h` — bootstrap exchange format.
2. `include/pymergetic/metal/port/plat.h` — probe trio (`machine_ram`, `link_used`, `arena_budget`).
3. Engine: `pm_sys.c` + `pm_hostinfo.c` on **linux and zephyr in parallel**.
4. Orchestrator: `pm_sys.c` + `orchestrator/boot.c` — one `/sys/pm` read, print `machine_ram`, `ready`.
5. CI: same `orchestrator.wasm` on both engines; stdout matches.

Then extend exchange types and `pm_mem` one piece at a time.
