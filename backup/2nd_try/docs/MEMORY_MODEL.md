# Memory model

Authoritative design for pymergetic-metal RAM: slot accounting, arena behavior, and how probe results reach the orchestrator upper.

See [PLATFORM.md](PLATFORM.md) for contract/upper/lower split · [LAYERS.md](LAYERS.md) for module matrix.

---

## Upper + lower, not lower-only

Memory **policy** lives in **upper** metal (`orchestrator/boot`, `mem`). **Lower** probes hardware and publishes host info for wasm mods.

| Module | Lower role | Upper role |
|--------|------------|------------|
| **`sys`** | Read `port/` probes, encode bootstrap | **In-process:** receive struct from lower, cache getters |
| **`hostinfo`** | Publish encoded blob → `/sys/pm` (wasm mods) | — |
| **`mem`** | Lower-side shim during bring-up (later minimal) | **Primary allocator** — malloc, mmap for upper + mods |
| **`orchestrator/boot`** | — | Layout report, arena sizing from `sys` |

**`port/` is not the memory model.** Lower-only mechanism.

**Upper bootstrap path (in-process):**

```
lower port/*.c  →  lower sys/sys.c (encode)  →  upper sys/sys.c (cache)
                                              →  upper orchestrator/boot.c
```

**Wasm mod bootstrap path (one-time handoff):**

```
lower hostinfo.c  →  write /sys/pm  →  mod hostinfo_load() at init
```

Upper does **not** read `/sys/pm` for itself — that round trip exists only for sandboxed wasm guests.

---

## Design rules

1. **Three separate questions** — never answer them with one number:
   - **Probe:** how much physical RAM does the machine have?
   - **Link window:** how much virtual address space is reserved for the kernel link image?
   - **Backing:** where does the userspace arena live in VA/PA?

2. **`CONFIG_SRAM_SIZE` / devicetree `zephyr,sram` is the link window only.** It is not machine RAM on dynamic x86 targets.

3. **Layout policy never includes OS headers.** Slot math and boot report live in upper `mem` / `orchestrator/boot`. OS probes live in lower `metal/port/` only.

4. **One userspace allocator per runtime.** Upper: one pool in `mem` (malloc + mmap siblings). Mods use upper arena or component shared blob.

5. **Kernel borrow only (lower path).** `k_malloc` is for lower-side, short-lived port bring-up — not the upper arena.

---

## Layers

```
┌──────────────────────────────────────────────────────────────┐
│ orchestrator/boot, mem           POLICY — upper (src/)       │
├──────────────────────────────────────────────────────────────┤
│ sys (upper + lower encode)       EXCHANGE — bootstrap record │
├──────────────────────────────────────────────────────────────┤
│ hostinfo (lower)                 /sys/pm for wasm mods only  │
├──────────────────────────────────────────────────────────────┤
│ metal/port/plat.h                  CONTRACT — probe API      │
├──────────────────────────────────────────────────────────────┤
│ metal/port/plat.c                  MECHANISM — OS-specific   │
│ (lower only)                                                 │
├──────────────────────────────────────────────────────────────┤
│ Zephyr kernel                    BORROW — k_malloc, MMU, E820│
└──────────────────────────────────────────────────────────────┘
```

### Metal slots (policy — upper `orchestrator/boot`)

| Slot | Meaning | Upper source | Lower source |
|------|---------|--------------|--------------|
| `machine_ram` | Total installed RAM (probe) | `sys` (in-process) | `pm_metal_port_machine_ram()` |
| `kernel_link` | Bytes from RAM base to `_end` | `sys` | `pm_metal_port_link_used()` |
| `kernel_static` | Link image minus in-link reserved buffers | arithmetic | arithmetic |
| `kernel_heap` | Kernel pool size | `sys` | `CONFIG_HEAP_MEM_POOL_SIZE` |
| `userspace_blob` | TLSF arena size | `mem` after `sys` budget | port backing + `mem` |

Accounting identity on real metal:

```
machine_ram  ≥  kernel_link + userspace_blob   (ideal: equality when blob uses all tail RAM)
```

### Port contract (lower only — feeds encode path)

```c
/* probe — encoded by lower sys, passed in-process to upper sys */
uint64_t pm_metal_port_machine_ram(void);
uint64_t pm_metal_port_link_used(void);
uint64_t pm_metal_port_arena_budget(void);

/* userspace arena backing — native lower path only (planned) */
int       pm_metal_port_arena_establish(size_t want_bytes, uintptr_t *base_out, size_t *size_out);
void     *pm_metal_port_arena_alloc(size_t n);
void      pm_metal_port_arena_free(void *p);
void     *pm_metal_port_arena_map(size_t n, size_t align);
int       pm_metal_port_arena_unmap(void *p, size_t n);
```

Upper code never calls port probes directly during boot — lower encodes and hands off the struct.

---

## Data flow

### Upper (in-process)

```
lower port/*.c     lower sys/sys.c (encode)
  probe           →    pm_metal_sys_bootstrap_t
                              │
                              ▼ (pointer / copy at boot)
                    upper sys/sys.c (cache)
                              │
                              ▼
                    upper mem/mem.c    orchestrator/boot.c
                    arena sizing       layout report
```

### Wasm mods (`/sys/pm`)

**`/sys/pm` is a one-time bootstrap handoff for sandboxed guests** — not a runtime syscall surface for the upper. Lower writes probe records before a mod starts. Mod `pm_metal_sys_hostinfo_load()` reads once, decodes, closes fd.

```
lower port/*.c        lower sys/sys.c         hostinfo.c
  probe              →    encode records    →    write /sys/pm (once)
                                                    │
                         mod boot: hostinfo_load()
                                                    │
                                                    ▼
                                              mod uses cached getters
```

---

## What Zephyr provides (lower port — use vs ignore)

| Zephyr facility | Use for pymergetic | Ignore for |
|-----------------|-------------------|------------|
| Multiboot E820 → `x86_memmap[]` | **Machine RAM probe** on GRUB/QEMU `-kernel` | EFI boots |
| `k_mem_map()` / `k_mem_unmap()` | Anonymous VM for arena chunks (after probe + publish) | Direct app malloc |
| `k_mem_map_phys_bare()` | Map known physical tail after `_end` | RAM discovery |
| `k_malloc` / `CONFIG_HEAP_MEM_POOL_SIZE` | Lower-side scratch (probe copies, drivers) | App / modules |
| `CONFIG_SRAM_SIZE` / DT | **Link map window** | Machine RAM on EFI |
| [sys_mem_blocks](https://docs.zephyrproject.org/latest/kernel/memory_management/sys_mem_blocks.html) | Fixed DMA block pools (if needed later) | General allocator |
| shared_multi_heap | Extending `k_heap` | Userspace TLSF arena |
| ZMS | Flash KV | RAM |

Zephyr does **not** expose “all remaining RAM” as one heap. Lower port composes: probe → establish backing → encode → (optional) publish to `/sys/pm` for mods → upper `mem` owns the allocator.

---

## Boot paths (lower port → sys)

### A. Fake metal (`native_sim`)

| Stage | Behavior |
|-------|----------|
| Probe | DT `zephyr,sram` size (host buffer cap) |
| Link window | Same DT region |
| Arena backing | Static `.bss` blob (`CONFIG_PM_USERSPACE_BLOB_SIZE`) |
| `machine_ram` source | `devicetree` |

### B. Real metal — multiboot (QEMU `-kernel`, GRUB)

| Stage | Behavior |
|-------|----------|
| Probe | Sum of `x86_memmap[RAM]` from E820 (`CONFIG_MULTIBOOT_MEMMAP=y`) |
| Link window | DT / `CONFIG_SRAM_SIZE` (can be smaller than probe total) |
| Arena backing | `k_mem_map_phys_bare` from physical page at `_end` for `(machine_ram - kernel_link)` |
| `machine_ram` source | `multiboot E820` |

### C. Real metal — EFI (VirtualBox, OVMF)

| Stage | Behavior |
|-------|----------|
| Probe | `metal/port/efi_ram.c` |
| Link window | DT / `CONFIG_SRAM_SIZE` (96 MiB today) |
| Arena backing | `k_mem_map_phys_bare` from `_end` for `pm_metal_port_arena_budget()` |
| `machine_ram` source | `EFI memory map` |

---

## Allocator choice (TLSF vs mimalloc)

| | **TLSF (current)** | **mimalloc (previous experiment)** |
|--|-------------------|-----------------------------------|
| Zephyr kernel | **Not used** — `k_malloc` uses `sys_heap` | N/A |
| Upper `mem` arena | Single pool, O(1) malloc/free | Separate page heap + segments |
| `mmap` semantics | Anonymous maps carve from same pool | Closer to OS page semantics |
| Server OS fit | Good when one bounded arena + predictable latency | Better for heavy concurrency / huge allocs |

**Decision:** one arena implementation inside upper **`mem`**. Swap TLSF for mimalloc later without changing `sys` exchange types or mod code.

Public introspection: `include/pymergetic/metal/mem/mem.h` (`pm_metal_mem_info_get` — planned).

---

## Userspace arena (`mem` — OS-agnostic behavior)

One contiguous TLSF pool:

```
arena [base, base + size)
  ├── malloc / calloc / realloc / free   → tlsf_*
  └── mmap / munmap (anonymous only)     → tlsf_memalign / tlsf_free
```

- **Upper:** arena sized from `sys` `arena_budget`; backing from lower port establish path.
- **Mods:** wasm linear memory + WASI `mmap` where available; or upper arena via component link.
- `mmap` rejects file-backed mappings (`ENOTSUP`).
- Page alignment for mmap comes from `CONFIG_MMU_PAGE_SIZE` on real metal lower port.

---

## Configuration map (lower port / Zephyr)

| Kconfig | Role |
|---------|------|
| `CONFIG_SRAM_SIZE` | Link map window (kernel image + k_pool) |
| `CONFIG_HEAP_MEM_POOL_SIZE` | Kernel heap — not app heap |
| `CONFIG_KERNEL_VM_SIZE` | Cap on anonymous VM for arena maps |
| `CONFIG_MULTIBOOT_MEMMAP` | E820 probe (multiboot only) |
| `CONFIG_X86_MEMMAP` | Page-frame tracking from `x86_memmap[]` |
| `CONFIG_PM_USERSPACE_BLOB` | Enable TLSF arena + mmap wrap |
| `CONFIG_PM_USERSPACE_BLOB_SIZE` | Fake metal only — static arena bytes |

---

## Implementation status

| Item | Status |
|------|--------|
| Metal layout slots + boot report (upper) | Planned — `src/pymergetic/metal/orchestrator/` |
| TLSF arena + malloc/mmap wrap | Planned — upper `mem/` |
| `metal/port/plat.h` + lower `port/plat.c` | Partial — zephyr + linux probes |
| Lower encode + upper in-process handoff | Planned (replacing orchestrator wasm path) |
| Lower `hostinfo` → `/sys/pm` for mods | Partial |
| Multiboot E820 probe | Partial |
| EFI SMBIOS probe | Reference in `backup/1st_try/` |
| Linux lower `port/plat.c` | Partial |

---

## What we removed (intentionally)

- **Orchestrator as wasm** — upper linked natively; no `orchestrator.wasm` / embed script.
- **Upper self-read via `/sys/pm`** — bootstrap passed in-process from lower.
- **Runtime SMBIOS probe under EFI** — wrong layer; caused boot instability.
- **Using `CONFIG_SRAM_SIZE` to fake machine RAM** — confuses link window with probe total.
