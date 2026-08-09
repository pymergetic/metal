# Definitions — filesystem / VFS

## One module law

Every first-party package is a **wasmmod pack** (MPWP in a `.wasm` / `.aot` / `.elf`
artifact — see wasmmod [`docs/PACK.md`](../../../wasmmod/docs/PACK.md)).

| Face | Role |
|------|------|
| **packload** | import / exec / exports → `sys.modules` |
| **fs_wasmmod** | RO VFS tree at `/mods/<pack.name>` |

Same law for `pymergetic.wasmmod`, `pymergetic.metal`, guests. No privileged
`host_self` / ROM-embed seed as the product file store. **Not mtar** for `/mods`.

```text
/mods/<pack.name>/…     ← fs_wasmmod RO (paths = pack-relative paths)
```

Pack-relative path + mount = VFS path (no remap). Example:

`httpd.json` root → `/mods/pymergetic.metal.inspect/www/inspect`

---

## Layers (product LIVE)

```text
upy / guest / ASGI
    pm_metal_fs_*_async  →  async handle
        |
        v
fs/                  fd table + path normalize
        |  resolve
        v
vfs/                 mount table (longest prefix)
        |
        v
fs/wasmmod           MPWP RO (product /mods)
fs/tmpfs             scratch (optional; not /mods)
```

| Rule | Meaning |
|------|---------|
| Public I/O | `*_async` returns completed/awaitable handles |
| Ops ABI | `pm_metal_fs_ops_t` + vfs mount |
| `/mods/<fqn>` | **wasmmod pack** only |
| mtar / fat / zip | In-tree; not the `/mods` product path |

---

## Mount tree (product LIVE)

Metal image mounts **metal** packs only (build → `$(BUILD)/packs/`, not checked into `src/`).
`pymergetic.wasmmod` is produced/mounted by **wasmmod** (`embed-host` / host pack VFS), not forged here.

```text
/mods/
├── pymergetic.metal/             # ASGI host (httpd.json)
├── pymergetic.metal.inspect/     # Inspect UI + contract
├── pymergetic.wasmmod/           # when wasmmod provides a real self pack
└── <guest.fqn>/                  # loaded guests (same fstype)
```

| Path | Fstype | Who |
|------|--------|-----|
| `/mods/<pack.name>` | fs_wasmmod RO | bringup / mod load |
| `/tmp` | tmpfs (later) | scratch — not modules |

Bringup: `mem_init` → async → mount build-generated metal packs → ASGI.

```text
pm_metal_fs_wasmmod_mount_mpwp(target_or_null, mpwp, len)
  → mounts at /mods/<MPWP.name> when target is null
```
