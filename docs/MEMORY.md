# Memory model — pools, linear memory, mounts

Compact comparison across platforms. Keep these knobs in lockstep or Zephyr
instantiate fails with `allocate linear memory failed`.

## Guest linear memory vs host pools

| Quantity | Linux | Zephyr native_sim | Zephyr qemu_x86_64 |
|----------|-------|-------------------|--------------------|
| Mod build `--max-memory` | **4 MiB** shared (`scripts/build-mod.sh`) | same | same |
| Linear mem backing | host `mmap` (not from kheap) | WAMR `BH_MALLOC` → **kheap** | same |
| WAMR kheap pool | CLI `--memory=` (verify: 16 MiB) | capped **12 MiB** of 24 MiB static | capped **12 MiB** of `arena_budget` |
| Bytecode arena | CLI `--bytecode-memory=` (1 MiB) | prefer **4 MiB** | prefer **4 MiB** |
| `machine_ram` / budget | `sysconf` diag only | `STATIC_POOL` 24 MiB BSS | E820 / `CONFIG_SRAM` − link − kernel heap |

**Invariant (Zephyr):**

```text
PM_METAL_ZEPHYR_MEMORY_REQ  ≥  2 × MAX_MEMORY  + WAMR slack
```

WAMR reserves the **full** shared-memory max at instantiate. Multimod can hold
two instances. Raising `MAX_MEMORY` without raising
`src/zephyr/main.c`'s `MEMORY_REQ` (and native_sim
`CONFIG_PM_METAL_STATIC_POOL_BYTES`) re-breaks Zephyr.

Linux `--memory=` only sizes WAMR's internal EMS/kheap bookkeeping — it does
**not** size guest linear memory.

## VFS / disks

| Mount | Linux | Zephyr | Size |
|-------|-------|--------|------|
| Root Stage A | `hostdir:<dir>` | FAT on DT `RAM` @ `/RAM:` | Zephyr: **8 MiB** ramdisk |
| tmpfs `scratch` | `/dev/shm` via `mkdtemp` | littlefs on DT `scratch` | Zephyr: **1 MiB** |
| tmpfs `other` | same | littlefs on DT `other` | Zephyr: **1 MiB** |
| DT `scratch2` | — | present, unused by fstab | 1 MiB |
| Mount table / `/proc` buf | 8 mounts / 16 KiB | same | shared caps |

Zephyr ramdisk BSS ≈ 8+1+1+1 = **11 MiB** inside `link_used` on qemu.

## Related docs

- Threading / wasi-threads: [RUNTIME.md § Threading](RUNTIME.md#threading)
- Mount phases: [MOUNT.md](MOUNT.md)
- Layout: [SOURCETREE.md](SOURCETREE.md)
