# Memory model — pools, linear memory, mounts

Server-class layout is **one shared preference** on every platform
(`src/common/pymergetic/metal/memory/layout.h`, mirrored by
`scripts/lib/memory-layout.sh`):

| Pool | Size | Role |
|------|------|------|
| kheap | **256 MiB** | WAMR `Alloc_With_Pool` — guest linear mem (Zephyr), stacks, EMS |
| bytecode | **64 MiB** | raw `.wasm` buffers (`load_file` / `load_bytes`) |
| WASM stack | **16 MiB** | per-instantiate operand stack (default) |

Linux / NuttX CLIs omit `--memory=` / `--bytecode-memory=` /
`--stack-memory=` to get these defaults. Zephyr prefers the same sizes,
capped by probed/`STATIC_POOL` `arena_budget`. Platforms differ only in
**backing** (malloc / host mmap / BSS / MMU map), not in the split.

## Guest linear memory vs host pools

| Quantity | Linux / NuttX | Zephyr native_sim | Zephyr qemu_x86_64 |
|----------|---------------|-------------------|--------------------|
| Mod build `--max-memory` | **4 MiB** shared (`scripts/build mod none`) | same | same |
| Linear mem backing | host `mmap` (not from kheap) | WAMR `BH_MALLOC` → **kheap** | same |
| WAMR kheap pool | layout default (256 MiB) | prefer 256 MiB of `STATIC_POOL` | prefer 256 MiB of `arena_budget` |
| Bytecode arena | layout default (64 MiB) | prefer 64 MiB | prefer 64 MiB |
| `machine_ram` / budget | `sysconf` / probe diag | `STATIC_POOL` 512 MiB BSS | E820 / `CONFIG_SRAM` − link − kernel heap |

**Invariant (Zephyr):**

```text
PM_METAL_MEMORY_KHEAP_BYTES  ≥  2 × MAX_MEMORY  + WAMR slack
STATIC_POOL / arena          ≥  kheap + bytecode
```

WAMR reserves the **full** shared-memory max at instantiate. Multimod can hold
two instances. Raising `MAX_MEMORY` without keeping `layout.h` (and native_sim
`CONFIG_PM_METAL_STATIC_POOL_BYTES`) honest re-breaks Zephyr instantiate
(`allocate linear memory failed`).

On Linux/NuttX, `--memory=` sizes WAMR's EMS/kheap bookkeeping (and stacks);
guest linear memory is still host `mmap` and is **not** carved from that pool.

## VFS / disks

| Mount | Linux | Zephyr | Size |
|-------|-------|--------|------|
| Root Stage A | `hostdir:<dir>` | FAT on DT `RAM` @ `/RAM:` | Zephyr: large FAT for python stdlib |
| tmpfs `scratch` | `/dev/shm` via `mkdtemp` | littlefs on DT `scratch` | Zephyr: **1 MiB** |
| tmpfs `other` | same | littlefs on DT `other` | Zephyr: **1 MiB** |
| DT `scratch2` | — | present, unused by fstab | 1 MiB |
| Mount table / `/proc` buf | 8 mounts / 16 KiB | same | shared caps |

## Related docs

- Threading / wasi-threads: [RUNTIME.md § Threading](RUNTIME.md#threading)
- Mount phases: [MOUNT.md](MOUNT.md)
- Layout: [SOURCETREE.md](SOURCETREE.md)
