# Memory baseline

See [MEMORY_MODEL.md](MEMORY_MODEL.md) for the authoritative probe/backing/arena design.

## Machine RAM

| Target | `pm_metal_memory_layout_machine_ram()` |
|--------|----------------------------------------|
| **Fake metal** (`native_sim`) | DT `zephyr,sram` (host buffer cap) |
| **Real metal multiboot** (`qemu_x86_64 -kernel`) | Sum of multiboot E820 RAM regions (`x86_memmap[]`) |
| **Real metal EFI** (VBox, OVMF) | DT link window until EFI GetMemoryMap probe lands — see MEMORY_MODEL.md |

`runtime/zephyr/boards/pm_machine_ram.dtsi` still sizes the **kernel SRAM window** in devicetree (link map / `CONFIG_SRAM_SIZE`). It is **not** the runtime total on real x86 once `CONFIG_MULTIBOOT_MEMMAP=y`.

## Platform split (fake vs real metal)

| | **Fake metal** (`native_sim`) | **Real metal** (`qemu_x86_64`, HW) |
|--|-------------------------------|-------------------------------------|
| Userspace blob | In-link `.bss` (`CONFIG_PM_USERSPACE_BLOB_SIZE` > 0) | Flat SRAM tail after `_end` |
| Blob size | Board conf (must fit in link map) | DT SRAM window minus link |
| Blob layout | Full blob → one TLSF pool | same over SRAM tail |
| Kernel static | `link − k_pool − blob` | `link − k_pool` |
| Board conf | `runtime/zephyr/boards/native_sim_native_64.conf` | (none — uses `prj.conf` tail) |

## Userspace blob (TLSF only)

One contiguous **userspace blob** window. TLSF owns 100% of it:

```
blob [base, base + total)
  ├── libc malloc/calloc/realloc/free  → tlsf_*
  └── mmap/munmap (wrapped)            → tlsf_memalign / tlsf_free
```

- **libc malloc** → TLSF (`tlsf_malloc_port.c`, `-fno-builtin-malloc`).
- **POSIX mmap** → same pool (`__wrap_mmap` / `__wrap_munmap`).
- **No MMU carve-out** on fake metal: flat BSS blob at link.
- **Real metal with MMU** (`qemu_x86_64`): SRAM tail mapped once via `k_mem_map_phys_bare`; TLSF uses the mapped virtual window.
- **Blob memtest** (`port/common/memtest.c`): pattern write/verify before TLSF init; parallel runners in `port/zephyr/memtest_parallel.c` / `port/linux/memtest_parallel.c`.
- **Allocator bench** (`metal/memory/bench.c`): boot micro-benchmark after layout; host baseline via `scripts/run-linux-bench.sh` (glibc).

## Runtime layout (boot order)

| Slot | Source |
|------|--------|
| machine | fake: DT · real x86: E820 sum |
| kernel static | link image minus in-link heap buffers |
| kernel heap | `CONFIG_HEAP_MEM_POOL_SIZE` (`k_malloc`, in link) |
| userspace blob | fake: in-link blob · real: SRAM tail (TLSF) |
| kernel link | `&_end − sram base` (audit — full link map) |

## API

- `#include <pymergetic/metal/metal.h>`
- **Layout (free funcs):** `pm_metal_memory_layout_machine_ram()` · `…_kernel_link()` · `…_sram_tail()` · `pm_metal_memory_layout_heap_get()` · `pm_metal_memory_layout_heap_bytes()` · `pm_metal_memory_layout_get()` · `pm_metal_memory_layout_report()`
- **Layout (vtable):** `pm_metal_memory_layout_ops` re-exports those free funcs · heap slots use `pm_metal_memory_layout_heap_ops` wired to `pm_metal_port_*` free funcs
- **Heaps:** `pm_metal_memory_layout_heap_get()` · `pm_metal_memory_layout_heap_bytes()` · `pm_metal_memory_layout_heap_stats()`

## Config (`prj.conf`)

- `CONFIG_HEAP_MEM_POOL_SIZE` — fixed `k_malloc` pool
- `CONFIG_PM_USERSPACE_BLOB=y` — TLSF userspace blob (disables `CONFIG_COMMON_LIBC_MALLOC`)
- Fake metal: `CONFIG_PM_USERSPACE_BLOB_SIZE` in `native_sim_native_64.conf`
- Real metal: `CONFIG_PM_USERSPACE_BLOB_SIZE=0` — dynamic SRAM tail

## Dependencies

- [TLSF](https://github.com/mattconte/tlsf) — userspace blob allocator (malloc + mmap)
