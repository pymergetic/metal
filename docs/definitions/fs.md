# Definitions — filesystem / VFS

Async-first path I/O for Metal firmware under ``. Public names align
with product [`include/.../fs/fs.h`](../../include/pymergetic/metal/fs/fs.h);
exp2 backs them with vfs + fstype ops (not ESP).

---

## Layers

```text
upy / guest / shell
    pm_metal_fs_*_async  ->  async handle
        |
        v
fs/                  fd table + path normalize
        |  resolve (sync)
        v
vfs/                 mount table (longest prefix)
        |
        v
fs/mtar | fs/fat | fs/zip | fs/overlay | fs/littlefs | fs/tmpfs
        |
        v
dev/blk[/ram]        sectors (ram completes Ready immediately)
```

| Rule | Meaning |
|------|---------|
| Public I/O | `*_async` returns `pm_metal_async_handle_t` |
| Ops ABI | Public `pm_metal_fs_ops_t` + `ops_register` / `ops_lookup` |
| Stackless | No durable state on C/wasm stack across `await` |
| Sync shims | Host tools only — not the guest contract |

---

## Product face

exp2 `fs/__init__.rs` tracks product `fs.h` entrypoints on the VFS/ops path:
`open` / `close` / `fread` / `fwrite` / `fpread` / `fpwrite` / `lseek` /
`stat` / `fstat` / `readdir` / `mkdir` / `unlink` / `rename` / `fsync`, plus
path helpers `size_async` / `read_async` / `write_async`. In-RAM fstypes
complete Ready immediately. Dual-ABI buffer macros (`PM_METAL_FS_IO_PTR`)
stay in the product header for guest wasm.

Migration: switch product `fs.c` ESP dispatch to vfs resolve + ops lookup;
keep WASI module string `pymergetic.metal.fs`.

---

## Boot mount tree (kernel-owned root)

Stage A rootfs, then Stage B fstab. Modules attach packs under `/` via
`pm_metal_boot_mod_load` — they never replace `/`.

```text
/                                  # FAT RW — kernel root (seeded)
├── etc/fstab                      # Stage B mounts
├── tmp/                           # tmpfs via fstab
├── mods/
│   └── <module.id>/               # mtar — runtime pack
└── src/
    └── <module.id>/               # mtar — sources (optional)
```

| Path | Fstype | Who |
|------|--------|-----|
| `/` | fat RW | kernel boot Stage A |
| `/mods/<id>` | mtar (RO or RW) | kernel / `pm_metal_boot_mod_load` |
| `/src/<id>` | mtar (RO or RW) | kernel / `pm_metal_boot_mod_load` |
| `/tmp` | tmpfs | Stage B `pm_metal_boot_rootfs_fstab_apply` |

Kernel module id (default): **`pymergetic.metal`**.

Boot order: `mem_init` → console → **`pm_metal_boot_rootfs_mount_all`**
→ Stage A (`/` + kernel packs) → Stage B (`/etc/fstab`) → rest.

```text
pm_metal_boot_mod_load(id, mods_blob, mods_len, src_blob, src_len)
pm_metal_boot_mod_unload(id)   # umount /mods/<id> and /src/<id>
```

---

## Landed (former v1 non-goals)

| Feature | Status |
|---------|--------|
| In-place mutable mtar | `pm_metal_fs_mtar_mount_rw` + RW ops (rebuild/compact) |
| littlefs | `fs/littlefs/` + `forge img littlefs` |
| Separate `/tmp` + fstab Stage B | tmpfs + `pm_metal_boot_rootfs_fstab_apply` |
| Product `fs.h` face | exp2 implements product entrypoints on vfs |
| Wasm mod loader mounts | `pm_metal_boot_mod_load` / `_unload` |

---

## Source pack modes (Kconfig)

Under `./forge-cli config edit` -> **pymergetic.metal -> fs**:

| Mode | Meaning |
|------|---------|
| `none` | Do not embed or mount `/src/<kernel id>` |
| `human only` | Pack forge originals only (no banner faces) |
| `human + generated` | Human + forge-generated faces |

Build pipeline: `./forge-cli build` -> `forge config gen` ->
`forge img rootfs` → `metal mod sync` → compile/link.

Default root size is **4 MiB** (`PM_METAL_FS_ROOT_SIZE_MIB`).
