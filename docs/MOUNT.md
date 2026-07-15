# Mount

A real, Linux-like mount system: a mount table instead of one fixed `vfs_root`, multiple
fstypes (host dir passthrough, tmpfs, later real device FS), a boot-time `/etc/fstab`, and —
eventually — a guest-callable `mount()`/`umount()` for a real `busybox mount`.

See [RUNTIME.md § VFS root](RUNTIME.md#vfs-root-same-every-load) (what this replaces) ·
[LAYERS.md](LAYERS.md) · [SOURCETREE.md](SOURCETREE.md).

Status: **Phases 1–2 landed** (mount table + `hostdir` fstype, `/etc/fstab` Stage B, `--mount=`/
`--rootfs=` CLI, `--vfs-root=` kept as a deprecated alias) — see `src/common/pymergetic/metal/mount/`,
`scripts/verify-linux-mount.sh`. **Phase 3 landed on linux only** (`tmpfs` fstype, `/dev/shm`-backed,
named-source registry — see `mount/tmpfs.c`, `mount/tmpfs_registry.c`,
`scripts/verify-linux-tmpfs.sh`); **zephyr's own `tmpfs` is still a stub, blocked on `wasi/file.c` +
`device.h`** (see "Zephyr prerequisite" below). **Phases 4–6 (boot-time populate, guest
`mount()`/`umount()`, overlays/real device FS) are still design only, not implemented.**

---

## Key insight

WASI preview1 already has real multi-mount plumbing — this is mostly *generalizing what
exists*, not inventing a VFS layer from scratch.

- WAMR's `wasm_runtime_set_wasi_args_ex()` already takes an *array* of `map_dir_list` entries
  (`"<guest>::<host>"`), not just one. `runtime.c` today only ever builds **one**
  (`map_dir_entry`, `"/::<vfs_root>"`) and hands it in as a 1-element array — see
  `pm_metal_runtime_run_ex()`.
- The guest side (wasi-libc) resolves an absolute path against whichever preopened directory
  has the **longest matching prefix** on its own, in guest code
  (`__wasilibc_find_relpath`, `wasi/libc-find-relpath.h`) — the host does not need to do any
  path-routing for guest WASI file I/O itself. Multiple simultaneous mounts already work at
  that layer for free, once the host hands over more than one `map_dir_list` entry.

What is actually missing: a real mount **table** (replacing the single `vfs_root` string), more
**fstypes** than "host directory", the host-side loader's own path resolution
(`pm_metal_runtime_resolve_vfs_path()`, used to read `.wasm` bytecode off disk — a separate
concern from guest WASI I/O) generalized to the same table, and a guest-callable mount/unmount
surface for later.

---

## fstype vs. source — don't conflate a ramdisk with a filesystem

On real Linux, `<source> <target> <fstype>` are two different axes: a ramdisk (`/dev/ram0`) is
a **block device** — it needs a real filesystem (`vfat`, `ext2`, ...) formatted onto it before
`mount` can do anything with it. `tmpfs` is the one fstype that skips the device entirely
(`mount -t tmpfs tmpfs /mnt` — no `<source>` needed, the kernel manages pages in RAM directly,
no on-disk structure at all).

This design only ever needs the **fstype** axis, not a real "ramdisk" concept, because nothing
here is writing an actual block-device driver or an on-disk filesystem parser:

| fstype | `<source>` column | linux impl | zephyr impl |
|--------|-------------------|------------|-------------|
| `hostdir` | a real host directory path (required) | literal passthrough — whatever real filesystem the host has that directory on (ext4, tmpfs, NFS, doesn't matter) is the host's problem, not ours | n/a for now — no real mounted FS to point at yet |
| `tmpfs` | a **name** (see "Named ramdisks" below — not literally `none`) | delegates to the host's *own* real tmpfs — `mkdtemp()` under `/dev/shm`, then treated internally as an ordinary `hostdir` | no true device-less pseudo-fs on Zephyr, so `establish()` does **two** steps under one fstype: provision a `zephyr,ram-disk` block device, then `fs_mount()` littlefs/FAT onto it — the block-device step is an implementation detail of *this* fstype's own `bind`, never a separate mount-table entry or fstab line |

Later, real device-backed fstypes (Phase 6, not building yet) *do* need a meaningful
`<source>` — e.g. `littlefs /data littlefs <partition-or-image-path>` — because at that point
there's an actual persistent block device/image to name, unlike `tmpfs`/`hostdir` where the
"device" question is either delegated away (`hostdir`) or doesn't exist (`tmpfs`).

### Block device layer — yes, add it now, not in Phase 6

Zephyr's own storage stack already forces this split to exist: `fs_mount()` (littlefs or FAT)
can't run against nothing — it needs a `disk_access`-registered device first. So "provision a
ramdisk, then format it" isn't a `tmpfs`-specific hack to hide, it's a real, separate step
Zephyr's own API requires regardless — worth making a first-class, reusable primitive from the
start instead of burying it inside `tmpfs.c` and having to pull it back out for Phase 6 (real
partitions) later:

| Piece | Role | Scope |
|-------|------|-------|
| `mount/device.h` / `.c` | `pm_metal_mount_device_kind_t` (`RAMDISK` now; `PARTITION`/`IMAGE` in Phase 6) + ops-struct `establish(opts)` → opaque device handle, `release(handle)` | `impl: bind`, **zephyr-only for now** — linux's `tmpfs`/`hostdir` talk straight to the host's own already-mounted filesystem and never touch a device concept at all; leave a `/* not impl: bind — linux tmpfs delegates to host tmpfs directly, no device step */` placeholder there per SOURCETREE.md's own convention, same as any other target that has nothing to implement for a given symbol |
| `mount/tmpfs.c` (zephyr) | **composes**, doesn't own, the device step: `device.h`'s own `ramdisk` kind `establish()` → device handle, then `fs_mount()` littlefs onto it directly (no separate pluggable "littlefs fstype module" yet — one fixed, internal choice, since `tmpfs` only ever needs *a* scratch fs, not a *choice* of one) | zephyr |
| Phase 6's real device-backed fstypes (`littlefs`/`fat`, deferred) | reuse the *same* `device.h` primitive, swapping in a `PARTITION`/`IMAGE` device kind instead of `RAMDISK` — same "device, then format" shape either way | zephyr (+ whichever real block storage a target adds) |

`tmpfs`'s own fstab/mount-table contract does not change — this only reshapes what its zephyr
`establish()` calls internally, from one opaque blob into two named, independently testable
steps.

### The zephyr ram disk itself — devicetree-declared, not runtime-allocated

Concretely, per `backup/2nd_try/host/zephyr/boards/pm_ramdisk.overlay` (old try, reference
only, but the mechanism itself is real Zephyr, not something that attempt invented):

```dts
ramdisk0 {
	compatible = "zephyr,ram-disk";
	disk-name = "RAM";
	sector-size = <512>;
	sector-count = <2048>;      /* 512 * 2048 = 1 MiB, fixed at build time */
};
```

Plus `CONFIG_DISK_ACCESS=y` / `CONFIG_DISK_DRIVERS=y` / `CONFIG_DISK_DRIVER_RAM=y` in `prj.conf`.
This is a **devicetree node** — Zephyr's own `ram_disk` driver reads `sector-size`/
`sector-count` and reserves that many bytes of static image memory, then self-registers against
`disk_access_init("RAM")` during Zephyr's own driver init, **before** `main()` runs at all. Two
consequences that change what `device.h`'s zephyr `establish()` actually does and what it can't:

1. **`establish()` doesn't create the device — it's already there.** The real work is just
   `disk_access_init(disk_name)` (idempotent — safe to call again) and handing back `disk_name`
   (a plain string, e.g. `"RAM"`) as the opaque device handle `tmpfs.c` passes to `fs_mount()`.
2. **Size is a build-time constant per board, not a runtime `size=` option.** Unlike linux
   (where `mkdtemp()` under real `/dev/shm` genuinely can grow up to whatever the host's tmpfs
   allows), a fstab line's `size=8M` on zephyr can only be **validated** against whatever the
   board's own overlay already declared (query via `disk_access_ioctl(disk_name,
   DISK_IOCTL_GET_SECTOR_COUNT/SIZE, ...)`) — asking for more than that is a hard fail for that
   line (logged + skipped, per Stage B's existing non-fatal-per-line rule), not something
   `establish()` can grant on demand. Each zephyr board target needs its own
   `boards/<board>.overlay` (today: empty, just `.gitkeep`) sized generously enough up front for
   whatever that board's `tmpfs` use actually needs (e.g. Phase 4's embedded-image extraction).
3. `release()` is close to a no-op on zephyr — the backing memory is static (BSS/image-reserved
   either way), there's no real "free the ram disk" call the way linux's `release()` genuinely
   `rm -rf`s a real `mkdtemp()`'d directory.
4. **Multiple simultaneous zephyr `tmpfs` mounts need multiple DT nodes** (distinct
   `disk-name`s, e.g. `"RAM0"`/`"RAM1"`) decided at board-overlay-authoring time — not something
   the mount table or fstab can conjure at runtime, since the disk itself doesn't exist until
   it's declared in the tree. `fs_mount()` also needs its own distinct littlefs runtime-state
   struct per mount point (`FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(...)`, one per instance) — two
   `tmpfs` mounts can't share one.

Worth calling out plainly: this means "configurable ram disk (all platforms)" from the original
ask is **not symmetric** between targets — genuinely runtime-configurable on linux, but
board-overlay-configurable (i.e. a build-time decision per target image) on zephyr. Document
this at the CLI/fstab level too once Phase 3 actually lands, so `size=` doesn't silently mean
two different things on the two targets.

Zephyr also has its *own*, separate, devicetree-native `zephyr,fstab` binding (`compatible =
"zephyr,fstab"`, `automount`/`disk-access`/`mount-point` properties — visible in that same old
overlay) that can auto-mount a filesystem before `main()` runs, no C code at all. That's a
plausible simplification for **Stage A's root mount specifically** on zephyr — `pm_metal_mount()`
at `init()` would then just *record* that Zephyr's own automount already happened (for
`resolve()`/`map_dir_list` bookkeeping) rather than performing the mount itself. It's not a
substitute for Stage B: `/etc/fstab` has to stay a runtime-readable, guest-visible text file
(for busybox's own `mount -a` to eventually parse), which a static devicetree node can't be.

### Named ramdisks — addressing more than one

"There is exactly one anonymous tmpfs" doesn't scale to "one for `/tmp` scratch, one holding
builtin commands, ..." — `<source>` for `tmpfs` has to actually be a **name**, not the literal
`none` used in earlier examples above. Fixed:

```
# <source>    <target>   <fstype>   <options>   <dump>  <pass>
scratch       /tmp        tmpfs      size=8M     0       0
builtin       /bin        tmpfs      ro          0       0
```

How a name resolves to an actual backing differs completely per target — this is exactly where
`hostdir` vs. `tmpfs`'s per-target `establish()` earns its keep:

| Target | How a name is declared | Runtime-configurable? |
|--------|------------------------|------------------------|
| **linux** | **Nothing to declare.** First fstab/`--mount=` line (or later guest `mount()` call) referencing a given name creates it on the spot — `mkdtemp()` under `/dev/shm`, size from that line's own `size=` (or a default). A second line naming the same `scratch` just reuses the already-`establish()`'d instance (mounted at a *different* `<target>` if it says so — same backing, two guest paths) rather than creating a second one. | **Yes, fully** — any name, any size, at any time; this is the strictly more flexible side. |
| **zephyr** | Name must already exist as a `disk-name` in **that board's own overlay** (`boards/<board>.overlay`) — `establish("builtin", opts)` calls `disk_access_init("builtin")`, which only succeeds if a `zephyr,ram-disk` DT node with that exact `disk-name` was compiled in. Referencing an undeclared name is a normal Stage B per-line failure (logged, skipped), not a crash. | **No — build-time only.** Wanting a second, independently-sized named ramdisk means adding a second DT node to the overlay and rebuilding, not a runtime call. |

The registry that tracks "has name X already been `establish()`'d this boot" is private to
`mount/tmpfs.c` (or a small `tmpfs_registry.c` helper) — a name → backing lookup, separate from
`mount.c`'s own table (which is keyed by guest **target path**, since the same named ramdisk can
legitimately back more than one mount point at once, e.g. `builtin` mounted read-write at one
path for setup and read-only at another for guests). First `establish()` for a name wins; later
references to the same name reuse it and ignore any conflicting `size=`/opts on that later line
(a warning, not a Stage-B-failing error) — matches zephyr, where those opts were never anything
but validation against an already-fixed DT capacity anyway.

**Phase 4 — boot-time populate (refined):** not name→blob. After Stage B has mounted
everything, `populate_all()` walks a **global ordered registry of archive blobs** and extracts
each against guest `/` via `pm_metal_mount_resolve()` → host `mkdir`/`write_file`. Files land on
whichever mount already owns that path prefix (same longest-prefix rule as everything else).
"One tar per FS" is an *authoring* convention (`builtin.tar` only contains `bin/…` entries), not
an API key — the registry does not care which named tmpfs (or hostdir) ends up receiving a given
path.

| Piece | Role |
|-------|------|
| Blob format | ustar (reuse `util/tar.h`); optional lz4 block wrap (reuse `util/lz4.h`) — no custom tree format |
| Paths in tar | guest paths (`bin/ls`, `mods/foo.wasm`, …); leading `/` normalized away; always extract as if rooted at `/` |
| Registry | process-wide list of `{blob, len, uncompressed_len, flags}`; each embed `.c` calls `pm_metal_mount_populate_register(...)` when linked (constructor or generated `register_all()`) |
| `populate_all()` | after Stage B, before mods; host-side writes only (bypass WASI) so guest-facing `ro` mounts are fine |
| Port | `pm_metal_port_write_file()` + `pm_metal_port_mkdir()` (`mkdir -p` semantics) — `impl: bind` |
| `pack-image.sh` | dir → ustar [→ lz4] → generated `.c` that holds the bytes and registers them |

Boot order: Stage A → Stage B (fstab/CLI) → **`populate_all()`** → mods.

Stage A's root mount naming stays: `--rootfs=tmpfs:<name>[,size=…]` (not the earlier wrong
`--rootfs=tmpfs:size=32M`). A caller not naming one explicitly gets an implicit conventional
name (`root`).

---

## One hard limitation — document, don't fight

WASI preopens are wired into a module **once, at `wasm_runtime_instantiate()` time**
(`runtime.c`'s `run_ex()`: `set_wasi_args_ex()` → `instantiate()`, back to back, same lock). A
"process" here (`runtime/process.h`'s `spawn()`) is one such instance, for its whole life. A
mount registered *after* a process was spawned is invisible to that already-running process —
only to processes spawned (instantiated) *afterward*. This is a WASI preview1 spec property
(no runtime, WAMR included, has a "here's a new preopen" syscall — and wasi-libc only scans
`fd_prestat_get` once at startup anyway), not a WAMR bug and not something to patch upstream.

In practice this is fine: `mount` and a following `ls` are two separate process spawns already
(two separate `exec`s), each picking up the mount table as of its own spawn — the gap only
matters for a single long-running process expecting to see its own `mount()` call without
re-exec'ing. Real fix, if ever needed: stop delegating `path_open`/`fd_*` to WAMR's built-in
POSIX WASI backend and intercept them ourselves so every guest file op re-resolves against the
live table — a real custom WASI-host layer, tracked as Phase 6, not built until something
actually needs it.

---

## Boot sequence — two stages

Root can't come from `/etc/fstab` — that file lives *on* the rootfs, so something must already
be mounted at `/` before it can even be read. Real Linux has the same split (kernel `root=`
cmdline vs. `/etc/fstab` for everything after) — mirror it:

### Stage A — root mount (never from fstab)

| Target | Source today | Source proposed |
|--------|--------------|------------------|
| linux | `--vfs-root=<dir>` (hostdir, hardcoded kind) | `--rootfs=<fstype>:<source>` (`--rootfs=hostdir:/srv/app`, `--rootfs=tmpfs:root,size=32M` — `<source>` is a **name**, see "Named ramdisks" below, not a size); `--vfs-root=<dir>` kept as a deprecated alias for `--rootfs=hostdir:<dir>` |
| zephyr | n/a (stub, see below) | Kconfig/board-overlay-selected fixed choice — root's `tmpfs` `<source>` name and its DT `disk-name`/size are both decided by that board's own overlay, not argv |

Both call the same `pm_metal_mount(kind, "/", source, opts)` — only where the arguments come
from differs per target. Stage A failing is fatal, same as today's `init()` failing.

### Stage B — `/etc/fstab`, read once root exists

Right after Stage A succeeds, in `app.c` (the one place every target's `main.c` already funnels
through, before the mod-run loop starts): `pm_metal_mount_fstab_apply("/etc/fstab")`.

- Resolves `/etc/fstab` against the just-mounted root, reads it via the same
  `pm_metal_port_read_file()` already used for `.wasm` bytecode — no new port primitive needed
  for *this* part (Phase 4's populate step needs a *write* primitive; this doesn't).
- **File missing → no-op.** Zero behavior change if nobody adds one — fully backward compatible
  with today's single-`vfs_root` behavior.
- Real fstab column shape, so a pasted-in real fstab (or busybox's own line-splitting, once it
  exists here) needs no translation:

```
# <source>       <target>   <fstype>   <options>   <dump>  <pass>
scratch           /tmp       tmpfs      size=8M     0       0
/srv/data         /data      hostdir    ro          0       0
```

(`scratch` is a **name** — see "Named ramdisks" further down for what it resolves to per
target, and how to address more than one.)

  - `<fstype>` = one of our own registered fstype names (`hostdir`, `tmpfs`, later
    `littlefs`/`fat`/`overlay` — see "fstype vs. source" above for what `<source>` means for
    each) — a small string→ops-struct lookup, same shape as `memory/ops.c`'s `resolve()`.
  - `<dump>`/`<pass>` parsed but ignored — kept only so a real/reused fstab-line parser doesn't
    choke on them; no fsck, no dump utility here.
  - `<options>`: `ro`/`rw` → the mount table's `readonly` flag; kind-specific tokens
    (`size=8M`) handled by that kind's own `establish()`.
- **Per-line failure is non-fatal** — log, skip, continue to the next line (matches `mount -a`;
  a bad line doesn't stop boot). Only Stage A failing is fatal.
- **File order = mount order**, not auto-sorted by path depth — matches `mount -a`, and keeps
  behavior identical the day busybox's own `mount -a` takes this over instead of our parser.

### CLI `--mount=` (dev convenience, not a second mechanism)

`--mount=<fstype>:<source>:<target>[:opts]` (repeatable) is sugar for "one synthetic fstab
line, applied *after* real `/etc/fstab`" — same parser, same apply function, one code path. A
CLI mount on top of a path fstab already mounted just wins (last-mount-at-a-path-wins, same
rule real Linux uses for stacking mounts) — exactly the override behavior you want for ad hoc
testing without editing an image's fstab.

### After Stage B

Whatever currently follows `init()` (today: `app.c`'s scripted mode running the CLI-given mod
list) runs against the now-fully-mounted namespace. A future "look for a real init path instead
of a CLI mod list" step would hook in right here too — related but out of scope for this doc;
noted only so the seam is in the right place when that gets designed.

---

## Mount table + backend kinds

New module, `src/common/pymergetic/metal/mount/` — `impl: common`, ops-struct pattern (mirrors
`memory/ops.h`'s shared struct + per-kind getter, see SOURCETREE.md § "Ops-struct flavor of
`bind`"):

| File | Prefix | Role |
|------|--------|------|
| `mount.h` / `.c` | `pm_metal_mount_` | table CRUD (`mount()`/`umount()`/`list()`), `resolve(guest_path)` → host path + remainder (longest-prefix, mirrors wasi-libc's own algorithm — used by the *loader's* own reads, e.g. `.wasm` bytecode, not guest WASI I/O), `build_map_dir_list()` (emits one `"<guest>::<host>"` per directory-backed mount for `run_ex()`) |
| `ops.h` | — | shared `pm_metal_mount_ops_t` struct (`establish`/`release`, mirrors `memory/ops.h`) + `pm_metal_mount_kind_t` enum |
| `hostdir.h` / `.c` | `pm_metal_mount_hostdir_` | passthrough of a real host directory — `impl: bind`, trivial on linux (validate + `realpath`), `impl: common`-ish on zephyr too once its own FS is real (backing dir must already be a mounted FS there) |
| `device.h` / `.c` | `pm_metal_mount_device_` | **zephyr-only** ops-struct for block devices (`RAMDISK` kind now) — see "Block device layer" above; not yet added |
| `tmpfs.h` / `.c` | `pm_metal_mount_tmpfs_` | `impl: bind` per target — **linux landed**: `mkdtemp()` under `/dev/shm` (already real tmpfs = RAM), then registered internally as an ordinary hostdir (no device layer involved at all); **zephyr still a stub**: will call `device.h`'s `ramdisk` kind `establish()` for the device, then `fs_mount()` littlefs directly onto it — the one genuinely new per-target backend, blocked on `wasi/file.c` (see "Zephyr prerequisite" below) |
| `tmpfs_registry.h` / `.c` | `pm_metal_mount_tmpfs_registry_` | **landed**, `impl: common` — name → host-path + refcount bookkeeping shared by every target's own `tmpfs.c` (see "Named ramdisks" above); deliberately keyed by *name*, separate from `mount.c`'s own guest-path-keyed table |
| `fstab.h` / `.c` | `pm_metal_mount_fstab_` | Stage B parser/applier, `impl: common` (pure text + calls into `mount.h`, no per-target code) |

`PM_METAL_MOUNT_MAX` bounds the table (same style as `PM_METAL_RUNTIME_MAX_HANDLES`).

`runtime.c`'s `init()` stops hand-building `map_dir_entry` itself — it calls
`pm_metal_mount()` for the root (Stage A) instead; `pm_metal_runtime_resolve_vfs_path()` and
`run_ex()`'s `map_dir_list` both switch to `pm_metal_mount_resolve()` /
`pm_metal_mount_build_map_dir_list()`.

---

## Guest-callable `mount()`/`umount()` (for busybox)

WASI preview1 has no mount syscall at all — needs our own extension, same shape as
`util/{arena,log,size,lz4,tar}.h`'s wasi-style imports (own `NativeSymbol` table, own
`import_module` string), **but privileged**: gated behind `-DPM_METAL_BUILD_KERNEL`, per the
README's existing Visibility model (mounting is not something an arbitrary unprivileged mod
should get to do). This would be the first real consumer of that already-documented-but-not-yet-
written `metal/build.h` split — a small new file, not an existing dependency.

Proposed home: `include/pymergetic/metal/mount.h` (top-level mod-facing tree, *not* under
`util/` — this isn't a leaf utility like `arena`/`log`, it's a first-class privileged runtime
API), declarations visible only when `PM_METAL_BUILD_KERNEL` is defined.

- Signature mirrors what busybox's own `mount(source, target, filesystemtype, mountflags,
  data)` / `umount(target)` expect closely enough that a small shim header (own
  `sys/mount.h`-shaped wrapper — same trick as `wasi_socket_ext.h` for sockets, since plain
  wasi-libc has no `mount()` either) can translate busybox's real calls into this import with
  no changes to busybox's own source.
- Effects are subject to the "one hard limitation" above — visible to the next `run()` on any
  handle, not to an already-running process. Document this at the call site, not just here.
- `build-mod.sh` gets a `MOUNT` marker file (same convention as `REACTOR`/`SOCKET`) for a mod
  that needs `-DPM_METAL_BUILD_KERNEL` + this header's native symbols linked in.

---

## Source tree additions

Maps to [SOURCETREE.md](SOURCETREE.md)'s own tree — phase number in brackets, `NEW`/`CHANGED`
against what exists today:

```
packages/metal/
├── include/pymergetic/metal/
│   └── mount.h                      NEW [5]  privileged (-DPM_METAL_BUILD_KERNEL) guest API
│
├── src/common/pymergetic/metal/
│   ├── mount/                       NEW module
│   │   ├── ops.h                    [1]  shared pm_metal_mount_ops_t + kind enum
│   │   ├── mount.h / mount.c        [1]  table CRUD + resolve() + build_map_dir_list() — impl: common
│   │   ├── hostdir.h / hostdir.c    [1]  impl: bind (linux trivial; zephyr once its own FS is real)
│   │   ├── device.h                 [3]  zephyr-only block-device ops-struct + kind enum — see "Block device layer" — not yet added
│   │   ├── tmpfs.h                  [3]  DONE (linux) — shared ops-struct decl, impl: bind per target
│   │   ├── tmpfs_registry.h / .c    [3]  DONE — impl: common, name → host-path + refcount bookkeeping
│   │   └── fstab.h / fstab.c        [2]  impl: common — Stage B parser/applier
│   ├── port/platform.h              CHANGED [4]  + write_file()/mkdir() (impl: bind both plats) — not yet added
│   ├── runtime/runtime.c            CHANGED [1]  vfs_root/map_dir_entry hand-rolling → mount/ calls
│   └── app/app.c                    CHANGED [2]  calls fstab_apply() after init(), before mod-run loop
│
├── src/linux/
│   ├── main.c                       CHANGED [1,2]  --rootfs=, --mount= (--vfs-root= kept as alias)
│   └── pymergetic/metal/
│       ├── mount/
│       │   ├── hostdir.c            [1]  impl: bind — realpath() validate
│       │   └── tmpfs.c              [3]  DONE — impl: bind — mkdtemp() under /dev/shm, nftw() rm -rf on release
│       └── port/platform.c          CHANGED [4]  write_file()/mkdir() impl — not yet added
│
├── src/zephyr/
│   ├── Kconfig                      CHANGED [1]  CONFIG_PM_METAL_ROOTFS_NAME etc. (which
│   │                                             declared disk-name is root — size itself
│   │                                             lives in the overlay below, not Kconfig) — not yet added
│   ├── boards/<board>.overlay       NEW [3]  per-board `zephyr,ram-disk` DT node(s) — name(s)
│   │                                          + size fixed here, not at runtime; see "The
│   │                                          zephyr ram disk itself" / "Named ramdisks" above
│   │                                          (dir exists today, empty/.gitkeep)
│   └── pymergetic/metal/
│       ├── wasi/file.c              CHANGED [3, prereq]  real os_* backend over zephyr's own
│       │                                                  POSIX open/read/write/opendir/readdir
│       │                                                  (not raw fs_*) — currently a stub, see below
│       ├── mount/
│       │   ├── device.c             [3]  impl: bind — RAMDISK kind: registers a zephyr,ram-disk disk_access device — not yet added
│       │   └── tmpfs.c              [3]  STUB landed (always fails — blocked on wasi/file.c + device.c above)
│       └── port/platform.c          CHANGED [4]  write_file()/mkdir() impl — not yet added
│
├── mods/
│   ├── t12_tmpfs_write/main.c       DONE [3]  writes through a tmpfs mount
│   ├── t13_tmpfs_read/main.c        DONE [3]  reads it back from a separate process
│   ├── t14_tmpfs_read_alt/main.c    DONE [3]  reads via a second fstab line naming the same source — proves reuse
│   ├── t15_tmpfs_read_other/main.c  DONE [3]  reads a differently-named source — proves independence
│   ├── t1x_mount_write/main.c       NEW [5]  calls mount() — own MOUNT marker file
│   └── t1y_mount_read/main.c        NEW [5]  reads back through the new mount
│
├── scripts/
│   ├── build-mod.sh                 CHANGED [5]  MOUNT marker (same convention as REACTOR/SOCKET)
│   ├── pack-image.sh                 NEW [4]  dir -> embedded C array (src/zephyr/generated/,
│   │                                          already anticipated by .gitignore's own comment there)
│   ├── verify-linux-mount.sh         DONE [2]
│   └── verify-linux-tmpfs.sh         DONE [3] (linux only)
│
└── docs/
    └── MOUNT.md                     this file
```

No changes anywhere in `src/rump/` or `src/unikraft/` — both are still stubs (README.md), out of
scope until either target itself is brought up.

---

## Phased plan

| # | Phase | Adds | Depends on |
|---|-------|------|------------|
| 1 | Mount table refactor | `mount/` module; single root mount, behavior identical to today | — |
| 2 | `/etc/fstab` + `--mount=` | Stage B parser/applier; CLI sugar as synthetic fstab line | 1 |
| 3 | `tmpfs` fstype + `device.h` layer | **linux: done** (`/dev/shm`-backed hostdir + named-source registry); **zephyr: still stub** — new `device.h` (`RAMDISK` kind) + `tmpfs.c` composing it with `fs_mount()` littlefs | 1–2; zephyr also blocked on `wasi/file.c` (below) |
| 4 | Boot-time populate | build-time packer (dir → embedded C array, per `.gitignore`'s already-anticipated `src/zephyr/generated/`); new `pm_metal_port_write_file()`/`mkdir`; `pm_metal_mount_populate()` | 3 |
| 5 | Guest `mount()`/`umount()` | privileged wasi-style import + busybox shim header | 1–3 |
| 6 (later, non-blocking) | Overlay/union mounts, real device/partition FS, `mount`-with-no-args listing (`/sys/mounts`, ties into RUNTIME.md's own not-yet-built virtual `/sys/loader` idea), live-remount custom WASI host (only if the "one hard limitation" above ever actually needs closing) | 1–5 |

### Zephyr prerequisite (blocks Phase 3+ testing on that target, not the design itself)

`src/zephyr/pymergetic/metal/wasi/file.c` is currently a stub (`pm_metal_wasi_file_init()`
just `return -1`). Needs a real WAMR `os_*` file backend before *any* zephyr mount — including
today's already-planned root `tmpfs` from `docs/RUNTIME.md` § "Bring-up plan" #5 — can work at
all.

**Checked directly against the vendored `external/zephyr` (4.4), not assumed, and not copied
from `backup/2nd_try/host/zephyr/pymergetic/wasi/zephyr_file.c`** (that file is still readable
for the `fs_*` struct shapes, but its own approach — hand-rolled against raw `fs_*`, one global
`prestat_dir`, single-preopen only — is exactly what this design corrects; not the model to
follow):

- Zephyr's own POSIX subsystem (`CONFIG_POSIX_API`, `lib/posix/options/fs.c` +
  `device_io.c`) genuinely provides real, fd-table-backed `open()`/`read()`/`write()`/
  `close()`/`stat()`/`fstat()`/`mkdir()`/`rmdir()`/`rename()`/`unlink()`/`opendir()`/
  `readdir()`/`closedir()` — each just forwards to `fs_*`/`zvfs_*` underneath. That's real,
  maintained upstream compat surface — the zephyr `os_*` backend should build on *these*, not
  go straight to raw `fs_*` structs itself (less custom code, tracks Zephyr's own fs stack for
  free).
- **Confirmed absent, grep'd, not assumed:** the whole `*at()` family — no `openat()`, no
  `fstatat()`, no `renameat()`, no `mkdirat()`, anywhere in Zephyr's POSIX layer. This — not
  "does zephyr have a real filesystem" — is the actual gap. It's why NuttX (which *does* have
  a full `*at()` family) gets to reuse WAMR's stock `external/wamr/.../platform/common/posix/
  posix_file.c` almost verbatim (a couple of `#ifdef __NuttX__` toggles — see
  `compilation_on_nuttx.yml`), while zephyr cannot: that door is closed for this target
  specifically, confirmed by reading the source, not by inference from the old attempt.
- Design for the new `os_*` backend: each `os_file_handle` (one per `os_open_preopendir()` —
  i.e. one per active mount-table entry, **not** the old backup's single global
  `prestat_dir`, which is what made it single-mount-only — plus one per intermediate directory
  WAMR opens while walking a multi-component `path_open()`) carries **its own** absolute base
  path. `os_openat(handle, relpath, ...)` becomes a plain absolute `open(handle->base_path +
  "/" + relpath, ...)` (via the real POSIX `open()` above) — synthesizing the missing `*at()`
  ourselves through string concatenation, since there is no real dirfd-relative primitive to
  delegate to either way. This is what makes it correct for our multi-mount table (several
  simultaneous preopens) instead of the old attempt's single mount.
- Two behavioral gaps are real, permanent, and platform-inherent — document them plainly
  (guest sees `ENOSYS`/zeroed fields, not a crash or a silently wrong answer), don't try to
  paper over them: **no symlinks** (`fs_*`/littlefs/FAT have none — `os_readlinkat`/
  `os_symlinkat`/`os_linkat` → `ENOSYS`, same as a real POSIX filesystem without symlink
  support would report) and **no real timestamps** (`fs_stat`/`fs_dirent` carry no mtime/atime
  — `os_futimens`/`os_utimensat` → `ENOSYS`, stat times read back as `0`).
- "Same behavior on both platforms" therefore does not mean identical `os_*` source (unlike
  Linux/NuttX to each other) — it means the same upper WASI logic
  (`sandboxed-system-primitives/src/posix.c`'s path resolution, rights checking, prestat
  handling, errno mapping) runs unchanged on both, with only this bottom primitive-translation
  layer differing — the same relationship Linux already has to Darwin/Windows/ESP-IDF today.

---

## Verify

| Script | Proves |
|--------|--------|
| existing `scripts/verify-linux*.sh` | Phase 1 changed nothing observable |
| `scripts/verify-linux-mount.sh` | multiple simultaneous mounts resolve correctly from real guest code, `--mount=` overriding a conflicting fstab line, `--vfs-root=` alias unregressed (Phase 2) |
| `scripts/verify-linux-tmpfs.sh` (**done, linux only**) | write from one mod (`t12_tmpfs_write`), read from another (`t13_tmpfs_read`), same `tmpfs` mount, gone after shutdown (a fresh process sees none of a prior run's content, and `/dev/shm/pm_metal_tmpfs_*` leftover count is unchanged) (Phase 3); `t14_tmpfs_read_alt` proves a second fstab line naming the same source reuses rather than re-creates it; `t15_tmpfs_read_other` proves two differently-named `tmpfs` sources stay independent |
| a `mods/t1x_mount_*` pair (new) | guest-side `mount()`/`umount()` round trip (Phase 5) |

---

## Done when

- [x] `mount/` module lands; single-root behavior byte-for-byte identical to today (Phase 1)
- [x] `/etc/fstab` parsed + applied at boot, missing file is a no-op (Phase 2)
- [x] `--mount=` CLI flag, linux-only, equivalent to a synthetic fstab line (Phase 2)
- [x] `tmpfs` fstype works on linux (`/dev/shm`-backed) (Phase 3)
- [ ] `device.h` (`RAMDISK` kind) lands on zephyr as its own reusable primitive, not buried in `tmpfs.c` (Phase 3)
- [ ] `tmpfs` fstype works on zephyr (real ram-disk + `fs_mount()`) — blocked on `wasi/file.c` (Phase 3)
- [x] two independently-named `tmpfs` sources coexist without clobbering each other; a repeated name reuses instead of re-creating (Phase 3, linux)
- [ ] compiled-in image extracted onto a *named* `tmpfs` mount (e.g. `builtin`) at boot, both targets, via one `pm_metal_mount_populate(name, blob)` call (Phase 4)
- [ ] guest-callable `mount()`/`umount()`, privileged-only, proven by a mod pair (Phase 5)
- [ ] the WASI-preopen limitation is documented at every call site that needs it, not just here
