# Memory model

Authoritative design for pymergetic-metal RAM: slot accounting, arena behavior, and how probe results reach the orchestrator.

See [PLATFORM.md](PLATFORM.md) for contract/policy/port split · [LAYERS.md](LAYERS.md) for module matrix.

---

## Engine + orchestrator, not engine-only

Memory **policy** lives in **orchestrator** metal (`orchestrator/boot`, `pm_mem`). The **engine** probes hardware and publishes host info.

| Module | Engine role | Orchestrator role |
|--------|-------------|-------------------|
| **`pm_sys`** | Read `port/` probes, encode → `/sys/pm` (via `pm_hostinfo`) | **Once at boot:** `fd_read` `/sys/pm`, decode, cache |
| **`pm_mem`** | Engine-side shim during bring-up (later minimal) | **Primary allocator** — malloc, mmap for orchestrator + instances |
| **`orchestrator/boot`** | — | Layout report, arena sizing from `pm_sys` |

**`port/` is not the memory model.** Engine-only mechanism. Flow: **port → engine `pm_sys` → `pm_hostinfo` → `/sys/pm` → orchestrator `pm_sys` → `pm_mem` / `orchestrator/boot`**.

---

## Design rules

1. **Three separate questions** — never answer them with one number:
   - **Probe:** how much physical RAM does the machine have?
   - **Link window:** how much virtual address space is reserved for the kernel link image?
   - **Backing:** where does the userspace arena live in VA/PA?

2. **`CONFIG_SRAM_SIZE` / devicetree `zephyr,sram` is the link window only.** It is not machine RAM on dynamic x86 targets.

3. **Layout policy never includes OS headers.** Slot math and boot report live in orchestrator `pm_mem` / `orchestrator/boot`. OS probes live in engine `metal/port/` only.

4. **One userspace allocator per runtime.** Orchestrator: one pool in `pm_mem` (malloc + mmap siblings). Instances use orchestrator arena or component shared blob.

5. **Kernel borrow only (engine path).** `k_malloc` is for engine-side, short-lived port bring-up — not the orchestrator arena.

---

## Layers

```
┌──────────────────────────────────────────────────────────────┐
│ orchestrator/boot, pm_mem        POLICY — slots, report, arena │
│ (orchestrator)                                                 │
├──────────────────────────────────────────────────────────────┤
│ pm_sys (engine + orchestrator)   EXCHANGE — /sys/pm records  │
├──────────────────────────────────────────────────────────────┤
│ metal/port/plat.h                  CONTRACT — engine probe API │
├──────────────────────────────────────────────────────────────┤
│ metal/port/plat.c, efi_ram.c       MECHANISM — OS-specific     │
│ (engine only)                                                  │
├──────────────────────────────────────────────────────────────┤
│ Zephyr kernel                    BORROW — k_malloc, MMU, E820│
└──────────────────────────────────────────────────────────────┘
```

### Metal slots (policy — guest `orchestrator/boot`)

| Slot | Meaning | Orchestrator source | Engine source |
|------|---------|---------------------|-----------------|
| `machine_ram` | Total installed RAM (probe) | `pm_sys` ← `/sys/pm` | `pm_metal_port_machine_ram()` |
| `kernel_link` | Bytes from RAM base to `_end` | `pm_sys` | `pm_metal_port_link_used()` |
| `kernel_static` | Link image minus in-link reserved buffers | arithmetic | arithmetic |
| `kernel_heap` | Kernel pool size | `pm_sys` | `CONFIG_HEAP_MEM_POOL_SIZE` |
| `userspace_blob` | TLSF arena size | `pm_mem` after `pm_sys` budget | port backing + `pm_mem` |

Accounting identity on real metal:

```
machine_ram  ≥  kernel_link + userspace_blob   (ideal: equality when blob uses all tail RAM)
```

### Port contract (engine only — feeds `pm_sys`)

```c
/* probe — encoded by engine pm_sys, consumed by orchestrator pm_sys */
uint64_t pm_metal_port_machine_ram(void);
uint64_t pm_metal_port_link_used(void);
uint64_t pm_metal_port_arena_budget(void);

/* userspace arena backing — native host path only (planned) */
int       pm_metal_port_arena_establish(size_t want_bytes, uintptr_t *base_out, size_t *size_out);
void     *pm_metal_port_arena_alloc(size_t n);
void      pm_metal_port_arena_free(void *p);
void     *pm_metal_port_arena_map(size_t n, size_t align);
int       pm_metal_port_arena_unmap(void *p, size_t n);
```

On the WASI path, orchestrator code never calls these — only engine `port/` + `pm_sys` do.

---

## Data flow

**`/sys/pm` is a one-time bootstrap handoff** — not a runtime syscall surface. Engine writes probe records before the orchestrator starts. Orchestrator `pm_metal_sys_init()` reads once, decodes into a cached struct, and closes the fd.

```
engine port/*.c        engine pm_sys.c         pm_hostinfo.c
  probe              →    encode records    →    write /sys/pm (once)
                                                    │
                         orchestrator boot: one fd_read /sys/pm
                                                    │
                                                    ▼
                    orchestrator pm_sys.c    pm_mem.c    orchestrator/boot.c
                    decode + cache         →  arena     → layout report
```

---

## What Zephyr provides (host port — use vs ignore)

| Zephyr facility | Use for pymergetic | Ignore for |
|-----------------|-------------------|------------|
| Multiboot E820 → `x86_memmap[]` | **Machine RAM probe** on GRUB/QEMU `-kernel` | EFI boots |
| `k_mem_map()` / `k_mem_unmap()` | Anonymous VM for arena chunks (after probe + publish) | Direct app malloc |
| `k_mem_map_phys_bare()` | Map known physical tail after `_end` | RAM discovery |
| `k_malloc` / `CONFIG_HEAP_MEM_POOL_SIZE` | Kernel-side scratch (probe copies, drivers) | App / modules |
| `CONFIG_SRAM_SIZE` / DT | **Link map window** | Machine RAM on EFI |
| [sys_mem_blocks](https://docs.zephyrproject.org/latest/kernel/memory_management/sys_mem_blocks.html) | Fixed DMA block pools (if needed later) | General allocator |
| shared_multi_heap | Extending `k_heap` | Userspace TLSF arena |
| ZMS | Flash KV | RAM |

Zephyr does **not** expose “all remaining RAM” as one heap. Host port composes: probe → (optional) publish to `x86_memmap[]` → establish backing → host `pm_sys` publishes → guest `pm_mem` owns the allocator.

---

## Boot paths (host port → pm_sys)

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

**Status: implemented** (native host path).

### C. Real metal — EFI (VirtualBox, OVMF)

| Stage | Behavior |
|-------|----------|
| Probe | `metal/port/efi_ram.c` (today: `src/pymergetic/port/zephyr/efi_ram.c`) |
| Link window | DT / `CONFIG_SRAM_SIZE` (96 MiB today) |
| Arena backing | `k_mem_map_phys_bare` from `_end` for `pm_metal_port_arena_budget()` |
| `machine_ram` source | `EFI memory map` |

**Status: implemented** in `src/pymergetic/port/zephyr/efi_ram.c` (target: `host/zephyr/pymergetic/metal/port/efi_ram.c`).

---

## Allocator choice (TLSF vs mimalloc)

| | **TLSF (current)** | **mimalloc (previous experiment)** |
|--|-------------------|-----------------------------------|
| Zephyr kernel | **Not used** — `k_malloc` uses `sys_heap` | N/A |
| Guest `pm_mem` arena | Single pool, O(1) malloc/free | Separate page heap + segments |
| `mmap` semantics | Anonymous maps carve from same pool | Closer to OS page semantics |
| Server OS fit | Good when one bounded arena + predictable latency | Better for heavy concurrency / huge allocs |

**Decision:** one arena implementation inside **`pm_mem`** (guest primary; host copy on native transitional path). Swap TLSF for mimalloc later without changing `pm_sys` exchange types or mod code.

Public introspection: `include/pymergetic/metal/memory/arena.h` today → `include/pymergetic/metal/pm_mem.h` (`pm_metal_arena_info_get`).

---

## Userspace arena (`pm_mem` — OS-agnostic behavior)

One contiguous TLSF pool:

```
arena [base, base + size)
  ├── malloc / calloc / realloc / free   → tlsf_*
  └── mmap / munmap (anonymous only)     → tlsf_memalign / tlsf_free
```

- **Guest:** arena sized from `pm_sys` `arena_budget`; backing is wasm linear memory + WASI `mmap` where available.
- **Native transitional host:** pre-arena bootstrap uses `k_malloc` only until `pm_metal_port_arena_establish()` completes.
- `mmap` rejects file-backed mappings (`ENOTSUP`).
- Page alignment for mmap comes from `CONFIG_MMU_PAGE_SIZE` on real metal host port.

---

## Configuration map (host port / Zephyr)

| Kconfig | Role |
|---------|------|
| `CONFIG_SRAM_SIZE` | Link map window (kernel image + k_pool) |
| `CONFIG_HEAP_MEM_POOL_SIZE` | Kernel heap — not app heap |
| `CONFIG_KERNEL_VM_SIZE` | Cap on anonymous VM for arena maps |
| `CONFIG_MULTIBOOT_MEMMAP` | E820 probe (multiboot only) |
| `CONFIG_X86_MEMMAP` | Page-frame tracking from `x86_memmap[]` — enable when probe publishes a map |
| `CONFIG_PM_USERSPACE_BLOB` | Enable TLSF arena + mmap wrap |
| `CONFIG_PM_USERSPACE_BLOB_SIZE` | Fake metal only — static arena bytes |

---

## Implementation status

| Item | Status |
|------|--------|
| Metal layout slots + boot report (native host) | Done — `src/pymergetic/metal/memory/` |
| TLSF arena + malloc/mmap wrap (native host) | Done — port + `pm_mem` path TBD |
| `metal/port/plat.h` + host `port/plat.c` | Done (today: `platform/plat.h` + `port/zephyr/plat.c`) |
| Host `pm_sys` → `/sys/pm` encode | Planned |
| Guest `pm_sys` + `orchestrator/boot` layout from exchange | Planned |
| Guest `pm_mem` as primary allocator | Planned |
| Multiboot E820 probe | Done |
| EFI GetMemoryMap in zefi → `efi_boot_arg` | **Reverted** — vanilla Zephyr only |
| `metal/port/efi_ram.c` SMBIOS probe | Done (vanilla Zephyr) |
| `pm_metal_arena_info_get` consumer API | Done |
| Linux host `port/plat.c` twin | Planned |

---

## What we removed (intentionally)

- **Runtime SMBIOS probe under EFI** — wrong layer (needs Boot Services or ACPI parser), caused boot instability, duplicated firmware work zefi can do once at handoff.
- **Using `CONFIG_SRAM_SIZE=524288` to fake 512 MiB machine RAM** — confuses link window with probe total.
