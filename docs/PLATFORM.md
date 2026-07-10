# Platform layers

How pymergetic-metal splits **contract**, **policy**, and **mechanism** so the same core runs on Linux (dev) and Zephyr (ship) without accidental OS-only APIs leaking upward.

See also: [LAYERS.md](LAYERS.md) · [SOURCETREE.md](SOURCETREE.md) · [MEMORY_BASELINE.md](MEMORY_BASELINE.md) for current RAM slot behavior on fake vs real metal.

---

## One rule

> **Metal decides meaning. Port defines the portable floor. Port impls lie about the OS so metal does not have to.**

`port/` is a **metal module** — host-only, but same symmetry row as `pm_mem`, `pm_sys`, etc. Contract in `include/pymergetic/metal/port/`; per-target impl in `host/<plat>/pymergetic/metal/port/`. `pm_host.c` and `wasi/` sit outside `metal/` as host siblings.

Dependency direction (strict):

```
host/<plat>/main.c
  → pymergetic/metal/*           policy — layout, boot, orchestrator, pm_*
    → metal/port/plat.h          contract — probe, map, alloc (no OS headers)
      → metal/port/plat.c, …       mechanism — ONLY layer that may include OS headers
```

**Today (transitional):** `runtime/<plat>/main.c` → `src/pymergetic/metal/*` → `src/pymergetic/platform/plat.h` → `src/pymergetic/port/{linux,zephyr}/*`. Same dependency shape; paths migrate per [SOURCETREE.md](SOURCETREE.md).

**Forbidden:** `#include <zephyr/...>`, `<unistd.h>`, `<pthread.h>`, `<linux/...>` anywhere under `include/pymergetic/`, `guest/`, or shared `metal/` policy sources.

**Allowed in port impl only:** OS headers, Kconfig (`CONFIG_*`), devicetree, board traits.

**Include tree:** `include/pymergetic/` is the orchestrator/host catalog. Wasm **mods** use a separate include path (`include/pymergetic/mod/` only) — not `PM_MAX_VIS` macro gating. See [Access levels](#access-levels).

Native freestanding mods (transitional) still use `export/pm_export_v1.h` via `scripts/build-mod.sh` until wasm mods land.

---

## Access levels

Not a security boundary — **minimal API surface** per build personality.

### Target (WASI) — two compile profiles

| Build | Includes | Kernel contact |
|-------|----------|----------------|
| **Orchestrator guest** | `include/pymergetic/**` | `pm_sys` API — one `fd_read` of `/sys/pm` at boot, then cached getters |
| **Mod `.wasm`** | `include/pymergetic/mod/` (+ WIT) only | stock WASI preopened `/sys/pm/*` if needed; orchestrator imports via component link |

Wasm mods do **not** use `PM_VIS_MOD` or `pm_export_v1`. Boundary = **what headers you link**, not macro stripping.

`pm_vis.h` stays for **orchestrator/host** only — gates `PM_VIS_RUNTIME` vs `PM_VIS_DEBUG`:

| Level | Value | Build | Sees |
|-------|-------|-------|------|
| `PM_VIS_RUNTIME` | 1 | orchestrator guest, `host/<plat>/` | `pm_sys`, `pm_mem`, orchestrator, … |
| `PM_VIS_DEBUG` | 2 | optional `-DPM_MAX_VIS=2` | runtime + debug-tagged API |

```c
#include <pymergetic/pm_vis.h>

PM_API(PM_VIS_RUNTIME, int, pm_metal_memory_boot, (void))
```

### Transitional (native `.o` mods)

Until wasm mods replace freestanding objects:

| Level | Value | Build | Sees |
|-------|-------|-------|------|
| `PM_VIS_MOD` | 0 | `scripts/build-mod.sh` | `export/pm_export_v1.h` only |

Mods compile with `-DPM_MAX_VIS=0 -I include`. **Drop this row** when `export/` is removed.

Header: `include/pymergetic/pm_vis.h`. Orchestrator/host default: `PM_MAX_VIS=1`.

---

## Three truths (do not mix)

| Truth | Question | Location (target) | Today (transitional) |
|-------|----------|-------------------|----------------------|
| **Contract** | What may metal code assume on every host target? | `include/pymergetic/metal/port/plat.h` | `src/pymergetic/platform/plat.h` |
| **Policy** | How do we boot, lay out RAM, load mods, run programs? | `include/pymergetic/metal/` + `guest/pymergetic/metal/` + shared host `metal/` | `include/pymergetic/metal/` + `src/pymergetic/metal/` |
| **Mechanism** | How does this OS do mmap / TLS / E820 / k_malloc? | `host/<plat>/pymergetic/metal/port/` (+ private headers there) | `src/pymergetic/port/{linux,zephyr}/` |
| **Mod import** | What may wasm mods call? | `mod/pm_mod.h` + WIT; `/sys/pm` via WASI preopen | same (transitional native `.o`: `export/pm_export_v1.h`) |

Today `src/pymergetic/metal/memory/layout.c` still mixes policy and Zephyr mechanism (DT, E820). That code moves into `metal/port/plat.c`; policy will call `pm_plat_*` only.

---

## Directory map

### Target

```
include/pymergetic/              orchestrator + host + guest catalog
├── pm_vis.h                     RUNTIME vs DEBUG (orchestrator/host only)
├── mod/                         wasm mod SDK
├── export/                      [transitional] pm_export_v1.h — native .o mods
└── metal/                       guest stack modules
    ├── orchestrator/            boot, loader, instance headers
    ├── port/plat.h              probe/map/alloc contract (host impl only)
    ├── pm_mem.h, pm_sys.h, orchestrator/…
    └── memory/                  [transitional] → pm_mem + orchestrator/boot (guest + host)

host/<plat>/                       native shell per target (linux, zephyr, rump, unikraft)
├── main.c, CMakeLists.txt, …
└── pymergetic/
    ├── metal/
    │   ├── orchestrator/mod_host.c
    │   ├── pm_mem.c, pm_sys.c, pm_types.c, posix.c, registry.c, vartree.c
    │   └── port/                  plat.c, efi_ram.c, traits.h, … (probes only)
    ├── wasi/wasi_impl.c           syscall glue (outside metal/)
    └── pm_host.c                  /sys/pm VFS writer (outside metal/)

guest/pymergetic/metal/            portable wasm32-wasip1 orchestrator stack (no port/)

mods/                              Example mod sources → .wasm
scripts/build-mod.sh               Mod build — PM_MAX_VIS=0
```

### Today (transitional)

```
src/pymergetic/
├── platform/plat.h                → include/pymergetic/metal/port/plat.h
├── metal/memory/                  → guest pm_mem + orchestrator/boot + host pm_sys (+ host pm_mem transitional)
└── port/{linux,zephyr}/           → host/<plat>/pymergetic/metal/port/

runtime/{linux,zephyr}/            → host/{linux,zephyr}/
```

Full tree: [SOURCETREE.md](SOURCETREE.md).

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

Policy code under `metal/` includes `metal/port/plat.h` only — never OS headers. Shared algorithms that sit on top of the contract (heap bookkeeping, etc.) live in host `metal/` `.c` files and must not include OS headers.

### `metal/` — product semantics (policy)

- **Memory layout slots:** machine, kernel static, k_pool, malloc, (future) mod arena — **`orchestrator/boot` + `pm_sys` + `pm_mem`** (guest + host)
- **Boot report** order and formatting — guest `orchestrator/boot`
- **Mod loader:** ET_REL parse (transitional) / wasm component link (target)
- **Process model:** program vs library, FRESH vs PERSIST, per-instance writable backing

**Guest** owns layout policy: `orchestrator/boot` reads cached `pm_sys` values (loaded once from `/sys/pm` at init), sizes arena via `pm_mem`.

**Host** `pm_sys` reads `port/` probes and encodes exchange records; `pm_host` writes `/sys/pm`. On the transitional native path, `src/pymergetic/metal/memory/` still calls `pm_plat_*` in-process — that folds into the symmetric modules.

```c
/* guest orchestrator/boot — target */
size_t machine = pm_sys_machine_ram();

/* transitional native host — today */
size_t machine = pm_plat_machine_ram();
```

Existing `pm_metal_memory_layout_ops` / heap vtables stay during native transition; target API is `pm_sys` + `pm_mem`.

### `metal/port/*.c` — adaptation (thick on purpose, host only)

| Concern | Linux | Zephyr |
|---------|-------|--------|
| Machine RAM | config / host probe | E820 sum or DT fallback |
| Link used | `_end - ram_base` | same |
| Kernel heap | mapped region + allocator | `k_malloc` pool |
| Userspace blob | TLSF pool (malloc + mmap) | flat SRAM tail; MMU map on real x86 |
| Map/unmap | `mmap`/`munmap` → TLSF tail | `--wrap=mmap` / `--wrap=munmap` |
| TLS | `pthread` keys | Zephyr TLS / thread local |
| VFS | host `open/read` | embedded image / littlefs later |
| Traits | (none) | fake vs real metal |

**Fake metal is not a third OS.** It is the Zephyr port with traits (`PM_ZEPHYR_FAKE_METAL`): in-link arena, DT window cap — same metal code, different `metal/port/plat.c` branch. Linux always uses host mmap; no fake twin.

Transitional files (`*_heap_port.c`, `platform.h`) fold into `metal/port/plat.c` as the contract lands.

---

## Memory: slots vs backings

| Metal slot (policy — guest `orchestrator/boot`) | Where values come from |
|-------------------------------------------------|------------------------|
| `machine_ram` | guest `pm_sys` ← host `pm_sys` ← port probe |
| `kernel_link` | guest `pm_sys` ← host `pm_sys` ← `pm_plat_link_used()` |
| `kernel_static` | arithmetic in `orchestrator/boot` |
| `kernel_heap` | guest `pm_sys` exchange record |
| `userspace_blob` | guest `pm_mem` sized from `pm_sys` `arena_budget` |
| `mod_heap` (future) | guest `pm_mem` / component shared blob |

Guest owns slot math and boot report. Host `port/` owns raw probes and native backing establishment only.

---

## Resemble Linux on Zephyr vs minimal interface

These apply at **different layers**:

| Goal | Where | How |
|------|-------|-----|
| Minimal, Zephyr-safe API | `metal/port/plat.h` | Small contract; new entries need dual-port proof |
| Fast dev on host | `host/linux` + `metal/port/` | Primary loop for loader / mod work |
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

1. **Twin compile** — `host/linux` and `host/zephyr` link the same guest-stack `metal/` policy lists; only `metal/port/` differs per target.
2. **Include lint (CI)** — fail if OS headers appear outside `host/<plat>/pymergetic/metal/port/`.
3. **New `plat.h` checklist** — Linux ok? Zephyr real metal ok? Fake metal ok enough for CI? Any no → keep helper private in `metal/port/`.
4. **Mod boundary** — wasm mods: `mod/` + WIT only. Native `.o` (transitional): `export/pm_export_v1.h` only.

---

## Migration from today

| Today | Becomes |
|-------|---------|
| `src/pymergetic/platform/plat.h` | `include/pymergetic/metal/port/plat.h` |
| `src/pymergetic/port/{linux,zephyr}/` | `host/<plat>/pymergetic/metal/port/` |
| `src/pymergetic/metal/memory/` | `guest/.../orchestrator/boot.c` + `guest/.../pm_mem.c` + `host/.../pm_sys.c` (+ host `pm_mem.c` transitional) |
| `layout.c` DT / E820 | `pm_plat_machine_ram()` in `metal/port/plat.c` |
| `port/zephyr/*_heap_port.c` | `pm_plat_alloc` in `metal/port/plat.c` |
| `port/zephyr/platform.h` (fake/real) | `metal/port/traits.h` |
| `runtime/{linux,zephyr}/` | `host/{linux,zephyr}/` |
| Heap port headers in `include/` | `metal/port/*_port.h` (private, host only) |

---

## First vertical slice (recommended)

Prove borders before full WASI migrate:

1. Move probe trio into `include/pymergetic/metal/port/plat.h` (from `src/pymergetic/platform/plat.h`).
2. Implement host `pm_sys.c`: read `pm_plat_*`, encode records; `pm_host.c` writes `/sys/pm`.
3. Implement guest `pm_sys.c` + `orchestrator/boot.c`: decode records, print same layout report as native path.
4. CI: native Zephyr binary and guest wasm print the same machine-RAM line (modulo fake vs real — see MEMORY_BASELINE.md).

Then extend exchange types and `pm_mem` one piece at a time — guest + host in the same change.
