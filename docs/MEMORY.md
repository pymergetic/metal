# Memory model — EFI / BIOS

Active targets are freestanding **efi** and **bios**. Hosted multi-host pool
layout (`kheap` / `bytecode` / `STATIC_POOL`) is gone with those ports.

## Host

| Pool | Role |
|------|------|
| Metal TLSF arena | Durable host + guest cookies (`pm_metal_mem_alloc`) — map / hole / heap dual-span |
| WAMR global heap | Interpreter / AOT runtime bookkeeping (`WASM_GLOBAL_HEAP_SIZE`) |
| Guest linear memory | Wasm instance memory — statics + short in-step stack; not for await-spanning buffers |

See [COOP_MEMORY.md](COOP_MEMORY.md) · [MODS.md](MODS.md) · [EFI.md](EFI.md).

## Guest rule

Anything that survives `await` lives on the **Metal host heap** (TLSF cookie),
not wasm linear `malloc`. Copy in/out with `pm_metal_mem_copy_*` when guest
code needs a linear view.

## Related

- Product heap / SMP: [COOP_MEMORY.md](COOP_MEMORY.md)
- Mods / durable frames: [MODS.md](MODS.md)
- Tree: [SOURCETREE.md](SOURCETREE.md)
