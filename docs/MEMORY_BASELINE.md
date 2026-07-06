# Memory baseline

## Machine RAM

| Target | `pm_metal_memory_layout_machine_ram()` |
|--------|----------------------------------------|
| **Fake metal** (`native_sim`) | DT `zephyr,sram` (host buffer cap) |
| **Real metal** (`qemu_x86_64`, HW) | **Sum of multiboot E820 RAM regions** (`x86_memmap[]`); DT is linker fallback only |

`runtime/boards/pm_machine_ram.dtsi` still sizes the **kernel SRAM window** in devicetree (link map / `CONFIG_SRAM_SIZE`). It is **not** the runtime total on real x86 once `CONFIG_MULTIBOOT_MEMMAP=y`.

## Platform split (fake vs real metal)

| | **Fake metal** (`native_sim`) | **Real metal** (`qemu_x86_64`, HW) |
|--|-------------------------------|-------------------------------------|
| Malloc backing | In-link `.bss` arena (`CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE` > 0) | Dynamic SRAM tail (`ARENA_SIZE=-1`) |
| Malloc size | Board conf arena (must fit in link map) | DT SRAM window minus link (libc still capped by `CONFIG_SRAM_SIZE`) |
| Kernel static | `link − k_pool − malloc_arena` | `link − k_pool` |
| Board conf | `runtime/boards/native_sim_native_64.conf` | (none — uses `prj.conf` tail) |

## Runtime layout (boot order)

| Slot | Source |
|------|--------|
| machine | fake: DT · real x86: E820 sum |
| kernel static | link image minus in-link heap buffers |
| kernel heap | `CONFIG_HEAP_MEM_POOL_SIZE` (`k_malloc`, in link) |
| malloc heap | fake: in-link arena · real: SRAM tail |
| kernel link | `&_end − sram base` (audit — full link map) |

## API

- `#include <pymergetic/metal/metal.h>`
- **Layout (free funcs):** `pm_metal_memory_layout_machine_ram()` · `…_kernel_link()` · `…_sram_tail()` · `pm_metal_memory_layout_heap_get()` · `pm_metal_memory_layout_heap_bytes()` · `pm_metal_memory_layout_get()` · `pm_metal_memory_layout_report()`
- **Layout (vtable):** `pm_metal_memory_layout_ops` re-exports those free funcs · heap slots use `pm_metal_memory_layout_heap_ops` wired to `pm_metal_port_*` free funcs
- **Heaps:** `pm_metal_memory_layout_heap_get()` · `pm_metal_memory_layout_heap_bytes()` · `pm_metal_memory_layout_heap_stats()`

## Config (`prj.conf`)

- `CONFIG_HEAP_MEM_POOL_SIZE` — fixed `k_malloc` pool
- Real metal: `CONFIG_COMMON_LIBC_MALLOC=y` + `ARENA_SIZE=-1`
- Fake metal: `native_sim_native_64.conf` — static in-link malloc arena
