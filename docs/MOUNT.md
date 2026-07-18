# Mount

A real, Linux-like mount system: a mount table instead of one fixed `vfs_root`, multiple
fstypes (host dir passthrough, tmpfs, virtual `proc`, later real device FS on Zephyr), a
boot-time `/etc/fstab`, and a guest-callable `mount()`/`umount()` (same-process visible on
linux via live remount).

See [RUNTIME.md § VFS root](RUNTIME.md#vfs-root-same-every-load) (what this replaces) ·
[LAYERS.md](LAYERS.md) · [SOURCETREE.md](SOURCETREE.md).

**Landed on linux (Phases 1–6c):** mount table, `hostdir`/`tmpfs`/`proc`, fstab/`--mount=`/
`--rootfs=`, populate, guest `mount()`/`umount()` with same-process visibility (live remount),
virtual `/proc`. **Zephyr still needs a real `wasi/file.c`** (tmpfs/device/proc/live remount
all blocked there). **Later:** real device/partition FS on Zephyr. Overlay/union is **out of
scope**.

---

## Key insight

WASI preview1 already has real multi-mount plumbing — this is mostly *generalizing what
exists*, not inventing a VFS layer from scratch.

- WAMR's `wasm_runtime_set_wasi_args_ex()` already takes an *array* of `map_dir_list` entries
  (`"<guest>::<host>"`), not just one. `run_ex()` builds that array from the live mount table
  every spawn (`pm_metal_mount_build_map_dir_list`).
- The guest side (wasi-libc) resolves an absolute path against whichever preopened directory
  has the **longest matching prefix** on its own, in guest code
  (`__wasilibc_find_relpath`, `wasi/libc-find-relpath.h`). On linux, Metal's `os_*` then
  re-resolves against the live table (live remount) so mounts added after instantiate are
  still visible.

**Landed on linux:** mount table (not a single `vfs_root`), `hostdir` / `tmpfs` / `proc`,
loader `resolve()` against that table, guest `mount()`/`umount()`, and Metal `os_*` live
remount so path ops re-resolve after instantiate. **Still open:** Zephyr `wasi/file.c` (+
tmpfs/device/proc on that target), optional real partition FS later.

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
| `proc` | ignored (conventionally `proc`) | sentinel `pm-metal:proc` + Metal `os_*` hooks (no host dir) | same contract once `wasi/file.c` is real |

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

Concretely, via a Zephyr `zephyr,ram-disk` overlay (the mechanism itself is real Zephyr):

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

## Preopen freeze — and live remount (linux)

WASI preopens are still wired **once** at `wasm_runtime_instantiate()` (`set_wasi_args_ex` →
`instantiate`). wasi-libc still scans `fd_prestat_get` only at startup — there is no "add
preopen" syscall. That is unchanged.

**On linux, Metal closes the visibility gap:** tagged preopen fds carry a guest prefix; every
`os_openat` / `os_*at` rebuilds the absolute guest path and calls `pm_metal_mount_resolve_ex`
against the **live** table. A `mount("/dyn", tmpfs)` is therefore visible to later opens in
the **same** process (routed through the frozen `/` preopen as relpath `dyn/...`). Open fds
keep their old backing (Linux-like). Proven by `scripts/verify linux none sys-mount` (t17
mounts + same-process use).

Virtual `/proc` was the first consumer of this `os_*` seam (Phase 6b); live remount extends
it to hostdir/tmpfs. Zephyr gets the same contract once its `wasi/file.c` is real.

---

## Procfs (virtual)

Guest `/proc` is Metal-internal state answered by **functions**, never a passthrough of the
host's own `/proc`, and never a directory of files written under `/tmp`.

### Wrong interim (Phase 6a — deleted)

```
hook → write file under /tmp/pm_metal_proc → WASI preopen as hostdir → guest read
```

That path is gone. It was not hook-on-open, raced across processes on cmdline/environ, and
needed host `/tmp` (dead on Zephyr).

### Landed (Phase 6b — linux)

```
guest open/read /proc/<node>
  → Metal WASI path layer (linux: wasi/file.c)
  → mount table: prefix is kind `proc` (preopen sentinel `pm-metal:proc`)
  → proc registry: lookup hook for <node>
  → hook generates bytes → memfd snapshot (dirs use opaque /dev/null + side table)
  → stock fd_read / lseek / fstat / close on that fd
```

| Piece | Contract |
|-------|----------|
| `pm_metal_mount_proc_register(name, fn)` | Keep — `name` is relative to the proc root (`"mounts"` → `/proc/mounts`). |
| `pm_metal_mount_proc_hook_fn` | Keep — `int (*)(char *out, size_t cap, size_t *out_len)`; may embed NULs (`cmdline` / `environ`). |
| Per-node generators | Under `mount/proc/*.c` (mounts, filesystems, version, cpuinfo, meminfo, uptime; cmdline/environ only via `/proc/self/`). |
| `proc` fstype `establish()` | Writes sentinel `pm-metal:proc` (no host dir). Table + WASI layer treat that preopen as virtual. |
| Open / read | On open of a registered node: call the hook once into a memfd. Global nodes regenerate each open (no `refresh()`). |
| Directory listing | `/proc` lists registered root hooks + `self/`; `/proc/self` lists `cmdline` + `environ`. |
| Process bind | `pm_metal_mount_proc_bind_current` / `unbind_current` around `run_ex` (TLS argv/env). |
| Dropped | `/tmp/pm_metal_proc`, `refresh()`, global `set_guest` as cmdline/environ source of truth. |

### Per-process nodes (`/proc/self`)

`/proc/cmdline` and `/proc/environ` are **not** one global file. v1 exposes them only as:

- `/proc/self/cmdline` — that process's argv (NUL-separated)
- `/proc/self/environ` — that process's WASI env (NUL-separated), including spawn's appended `PID=`

Bound to the calling process's slot (the env/argv actually passed into `run_ex` / `set_wasi_args_ex`), not a process-global `set_guest` overwrite. No `/proc/<pid>/` tree in v1 beyond `self`.

Shared nodes stay at the proc root and are process-independent:

`/proc/mounts`, `/proc/filesystems`, `/proc/version`, `/proc/cpuinfo`, `/proc/meminfo`, `/proc/uptime`.

### Relationship to other mounts

On linux, hostdir/tmpfs and `proc` share the same live-resolve `os_*` seam: preopens are still
established at instantiate, but path ops re-resolve against the live table. Zephyr remains
blocked on a real `wasi/file.c` for *all* mounts (same contract once that lands).

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

  - `<fstype>` = one of our own registered fstype names (`hostdir`, `tmpfs`, `proc`, later
    `littlefs`/`fat` — see "fstype vs. source" above for what `<source>` means for
    each) — a small string→ops-struct lookup, same shape as `memory/ops.c`'s `resolve()`.
    No overlay/union fstype.
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

### After Stage B — product defaults, then populate

`app.c` (and Zephyr verify's Stage B mirror) always ensure, if missing:

| Guest path | Kind | Notes |
|------------|------|--------|
| `/proc` | `proc` | Virtual procfs |
| `/tmp` | `tmpfs` source `tmp` | Named ramdisk / host tmpfs; Zephyr DT `disk-name = "tmp"` |

Then **`pkg_apply_all()`** extracts named lz4+ustar guest packages (`mods-tests`,
`python-stdlib`, `mods-apps-python`, …) onto `/`, then **`populate_all()`** extracts any
anonymous verify-only populate embeds. Missing `/etc/fstab` is normal for product boot —
content arrives via packages (and optional populate embeds), not via a multi-line scratch
fstab. Same package set is embedded on Linux, NuttX, and Zephyr (`scripts/lib/guest-pkgs.sh`).

**Test-only mounts** (`scratch` → `/scratch`/`/scratchB`, `other` → `/other`) live only in
verify scripts (`scripts/verify.d/port/linux/tmpfs.sh`, `populate.sh`, and Zephyr verify just
before those batches). They are not part of the product default boot layout.

Whatever currently follows (today: `app.c`'s scripted mode running the CLI-given mod list)
runs against the now-fully-mounted namespace. A future "look for a real init path instead of a
CLI mod list" step would hook in right here too — related but out of scope for this doc;
noted only so the seam is in the right place when that gets designed.

---

## Mount table + backend kinds

New module, `src/common/pymergetic/metal/mount/` — `impl: common`, ops-struct pattern (mirrors
`memory/ops.h`'s shared struct + per-kind getter, see SOURCETREE.md § "Ops-struct flavor of
`bind`"):

| File | Prefix | Role |
|------|--------|------|
| `table.h` / `table.c` | `pm_metal_mount_` | table CRUD (`mount()`/`umount()`/`list()`), `resolve(guest_path)` → host path + remainder (longest-prefix, mirrors wasi-libc's own algorithm — used by the *loader's* own reads, e.g. `.wasm` bytecode, not guest WASI I/O), `build_map_dir_list()` (emits one `"<guest>::<host>"` per directory-backed mount for `run_ex()`) |
| `ops.h` | — | shared `pm_metal_mount_ops_t` struct (`establish`/`release`, mirrors `memory/ops.h`) + `pm_metal_mount_kind_t` enum |
| `hostdir.h` / `.c` | `pm_metal_mount_hostdir_` | passthrough of a real host directory — `impl: bind`, trivial on linux (validate + `realpath`), `impl: common`-ish on zephyr too once its own FS is real (backing dir must already be a mounted FS there) |
| `device.h` / `.c` | `pm_metal_mount_device_` | **zephyr-only** ops-struct for block devices (`RAMDISK` kind now) — see "Block device layer" above; not yet added |
| `tmpfs.h` / `.c` | `pm_metal_mount_tmpfs_` | `impl: bind` per target — **linux landed**: `mkdtemp()` under `/dev/shm` (already real tmpfs = RAM), then registered internally as an ordinary hostdir (no device layer involved at all); **zephyr still a stub**: will call `device.h`'s `ramdisk` kind `establish()` for the device, then `fs_mount()` littlefs directly onto it — the one genuinely new per-target backend, blocked on `wasi/file.c` (see "Zephyr prerequisite" below) |
| `tmpfs_registry.h` / `.c` | `pm_metal_mount_tmpfs_registry_` | **landed**, `impl: common` — name → host-path + refcount bookkeeping shared by every target's own `tmpfs.c` (see "Named ramdisks" above); deliberately keyed by *name*, separate from `mount.c`'s own guest-path-keyed table |
| `populate.h` / `.c` | `pm_metal_mount_populate_` | **landed**, `impl: common` — anonymous ustar [+ lz4] blob registry + `populate_all()` / `populate_extract()` against guest `/` |
| `pkg.h` / `.c` | `pm_metal_pkg_` | **landed**, `impl: common` — named packages with dep graph; `pkg_apply_all()` / `pkg_ensure()` call `populate_extract()` |
| `fstab.h` / `.c` | `pm_metal_mount_fstab_` | Stage B parser/applier, `impl: common` (pure text + calls into `table.h`, no per-target code) |

`PM_METAL_MOUNT_MAX` bounds the table (same style as `PM_METAL_RUNTIME_MAX_HANDLES`).

`runtime.c`'s `init()` stops hand-building `map_dir_entry` itself — it calls
`pm_metal_mount()` for the root (Stage A) instead; `pm_metal_runtime_resolve_vfs_path()` and
`run_ex()`'s `map_dir_list` both switch to `pm_metal_mount_resolve()` /
`pm_metal_mount_build_map_dir_list()`.

---

## Guest-callable `mount()`/`umount()` (for busybox) — **landed**

WASI preview1 has no mount syscall at all — our own extension, same shape as
`util/{arena,log,size,lz4,tar}.h`'s wasi-style imports (own `NativeSymbol` table, own
`import_module` string `"pymergetic.metal.mount"`), **privileged**.

Compile-time: guest headers expose `mount()`/`umount()` only with
`-DPM_METAL_BUILD_KERNEL` (`include/pymergetic/metal/build.h` + empty
`mods/<name>/MOUNT` marker in `scripts/build mod none`). That is **not** a security
boundary — crafted wasm can still import the natives.

Runtime: host must opt in with `--allow-guest-mount` (linux CLI →
`pm_metal_runtime_set_allow_guest_mount(1)`). Without it, mount/umount
natives return -1; `fstype_count`/`fstype_name` stay public.

| Piece | Role |
|-------|------|
| `include/pymergetic/metal/build.h` | Visibility contract home (`PM_METAL_BUILD_KERNEL`) |
| `include/pymergetic/metal/mount/mount.h` | wasi-import contract (same shape as `util/*.h`): `pm_metal_mount_mount` / `pm_metal_mount_umount` + Linux-shaped `mount()`/`umount()` inlines — guest surface gated on `PM_METAL_BUILD_KERNEL` |
| `src/common/.../mount/table.h` / `table.c` | host-only mount table (`pm_metal_mount` / `resolve` / …) |
| `src/common/.../mount/mount.c` | wasi bridge + `pm_metal_mount_native_register()` (pairs with include/.../mount/mount.h) |

- Signature: `pm_metal_mount_mount(source, target, fstype, options)` — same field meaning as an
  fstab/`--mount=` line (tmpfs `source` is a name). Same header also exposes Linux
  `mount()`/`umount()` + `MS_RDONLY` as thin inlines over those imports.
- Effects update the live table immediately. On linux, path ops in the **same** process see
  them (live remount); later spawns also pick them up via a fresh `map_dir_list`. Proven by
  `scripts/verify linux none sys-mount` (t17 mount + same-process use → t18 still uses → t19
  umount → t20 gone).

---

## Source tree additions

Maps to [SOURCETREE.md](SOURCETREE.md)'s own tree — phase number in brackets, `NEW`/`CHANGED`
against what exists today:

```
packages/metal/
├── include/pymergetic/metal/
│   ├── build.h                      NEW [5]  Visibility (`PM_METAL_BUILD_KERNEL`)
│   └── mount/
│       └── mount.h                  NEW [5]  privileged mount()/umount() wasi-import contract (like util/*.h)
│
├── src/common/pymergetic/metal/
│   ├── mount/                       NEW module
│   │   ├── ops.h                    [1]  shared pm_metal_mount_ops_t + kind enum
│   │   ├── table.h / table.c        [1]  host-only mount table — impl: common
│   │   ├── proc.h / proc.c          [6b] virtual proc registry + sentinel establish; lookup/hooks/bind TLS
│   │   ├── proc/                    [6b] per-node generators + guest TLS (cmdline/environ via /proc/self)
│   │   ├── mount.c                  [5]  wasi bridge for include/.../mount/mount.h — impl: common
│   │   ├── hostdir.h / hostdir.c    [1]  impl: bind (linux trivial; zephyr once its own FS is real)
│   │   ├── device.h                 [3]  zephyr-only block-device ops-struct + kind enum — see "Block device layer" — not yet added
│   │   ├── tmpfs.h                  [3]  DONE (linux) — shared ops-struct decl, impl: bind per target
│   │   ├── tmpfs_registry.h / .c    [3]  DONE — impl: common, name → host-path + refcount bookkeeping
│   │   ├── populate.h / populate.c  [4]  DONE — global blob registry + populate_all() (ustar/lz4 → resolve → port write)
│   │   └── fstab.h / fstab.c        [2]  impl: common — Stage B parser/applier
│   ├── port/platform.h              CHANGED [4]  + write_file()/mkdir() (impl: bind both plats)
│   ├── runtime/runtime.c            CHANGED [1]  vfs_root/map_dir_entry hand-rolling → mount/ calls
│   └── app/app.c                    CHANGED [2,4]  fstab_apply + CLI mounts + populate_all() before mod-run loop
│
├── src/linux/
│   ├── main.c                       CHANGED [1,2]  --rootfs=, --mount= (--vfs-root= kept as alias)
│   ├── CMakeLists.txt               CHANGED [6b]  drop WAMR posix_file.c from vmlib; Metal wasi/file.c + posix_file_real.c
│   └── pymergetic/metal/
│       ├── mount/
│       │   ├── hostdir.c            [1]  impl: bind — realpath() validate
│       │   └── tmpfs.c              [3]  DONE — impl: bind — mkdtemp() under /dev/shm, nftw() rm -rf on release
│       ├── wasi/
│       │   ├── file.c               [6b, 6c] DONE — virtual proc + live remount resolve_ex; else __real_os_*
│       │   └── posix_file_real.c    [6b] DONE — WAMR posix_file.c with os_* → __real_os_*
│       └── port/platform.c          CHANGED [4]  write_file()/mkdir() impl — DONE (linux)
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
│       ├── wasi/file.c              CHANGED [3, 6b prereq]  real os_* backend + virtual proc open/read
│       │                                                  — currently a stub, see below
│       ├── mount/
│       │   ├── device.c             [3]  impl: bind — RAMDISK kind: registers a zephyr,ram-disk disk_access device — not yet added
│       │   └── tmpfs.c              [3]  STUB landed (always fails — blocked on wasi/file.c + device.c above)
│       └── port/platform.c          CHANGED [4]  write_file()/mkdir() stub until wasi/file.c + real FS
│
├── mods/
│   ├── t12_tmpfs_write/main.c       DONE [3]  writes through a tmpfs mount
│   ├── t13_tmpfs_read/main.c        DONE [3]  reads it back from a separate process
│   ├── t14_tmpfs_read_alt/main.c    DONE [3]  reads via a second fstab line naming the same source — proves reuse
│   ├── t15_tmpfs_read_other/main.c  DONE [3]  reads a differently-named source — proves independence
│   ├── t16_populate_read/main.c     DONE [4]  reads a file only present via populate_all() extract
│   ├── t1x_mount_write/main.c       NEW [5]  calls mount() — own MOUNT marker file
│   └── t1y_mount_read/main.c        NEW [5]  reads back through the new mount
│
├── scripts/
│   ├── build mod                    CHANGED [5]  MOUNT marker (same convention as REACTOR/SOCKET)
│   ├── lib/pack-image.sh            DONE [4]  dir → ustar [→ lz4] → generated .c that registers the blob
│   ├── verify linux mount           DONE [2]
│   ├── verify linux tmpfs           DONE [3] (linux only)
│   └── verify linux populate        DONE [4] (linux only)
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
| 4 | Boot-time populate | ustar [+ lz4] embeds; global `populate_register` list; `populate_all()` extracts against guest `/` via resolve + port `write_file`/`mkdir`; `pack-image.sh` | 3 (linux half enough to prove) |
| 5 | Guest `mount()`/`umount()` | **done** — privileged wasi import in `include/.../mount/mount.h` (bridge in `mount.c`) + `MOUNT` marker | 1–3 |
| 6a | Procfs hooks (interim) | **deleted** — materialize-to-`/tmp` path removed when 6b landed | 1–5 |
| 6b | Virtual `/proc` + WASI path intercept | **done (linux)** — sentinel establish, hook-on-open, `/proc/self/{cmdline,environ}` via TLS bind, Metal `os_*` shim; **zephyr still stub** (`wasi/file.c`) | 1–5, WASI file seam |
| 6c | Live remount | **done (linux)** — every `os_*at` re-resolves against the live mount table; same-process `mount()` visible; **zephyr deferred** with its `wasi/file.c` | 6b |
| 6 (later) | Real device/partition FS (`littlefs`/`fat`) on Zephyr. **Not** overlay/union — out of scope. | 1–5, 6c, Zephyr WASI |

### Zephyr prerequisite (blocks Phase 3+ testing on that target, not the design itself)

`src/zephyr/pymergetic/metal/wasi/file.c` is currently a stub (`pm_metal_wasi_file_init()`
just `return -1`). Needs a real WAMR `os_*` file backend before *any* zephyr mount — including
today's already-planned root `tmpfs` from `docs/RUNTIME.md` § "Bring-up plan" #5 — can work at
all.

**Checked directly against the vendored `external/zephyr` (4.4), not assumed.** An earlier
hand-rolled `fs_*` backend (one global `prestat_dir`, single-preopen only) is exactly what
this design corrects — not the model to follow:

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
  i.e. one per active mount-table entry, **not** a single global `prestat_dir` (which is what
  made an earlier attempt single-mount-only) — plus one per intermediate directory
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
| existing `scripts/verify linux none …` | Phase 1 changed nothing observable |
| `scripts/verify linux none mount` | multiple simultaneous mounts resolve correctly from real guest code, `--mount=` overriding a conflicting fstab line, `--vfs-root=` alias unregressed (Phase 2) |
| `scripts/verify linux none tmpfs` (**done, linux only**) | write from one mod (`t12_tmpfs_write`), read from another (`t13_tmpfs_read`), same `tmpfs` mount, gone after shutdown (a fresh process sees none of a prior run's content, and `/dev/shm/pm_metal_tmpfs_*` leftover count is unchanged) (Phase 3); `t14_tmpfs_read_alt` proves a second fstab line naming the same source reuses rather than re-creates it; `t15_tmpfs_read_other` proves two differently-named `tmpfs` sources stay independent |
| `scripts/verify linux none populate` (**done, linux only**) | Stage B mounts a `tmpfs` at `/scratch`; an embedded ustar (from `pack-image.sh`) registers `scratch/hello.txt`; `populate_all()` extracts against `/`; guest `t16_populate_read` sees the file (Phase 4) |
| `scripts/verify linux none sys-mount` (**done, linux**) | t17 `mount` + same-process use (live remount); t18 still uses across spawn; t19 umount; t20 gone (Phases 5 + 6c) |
| `scripts/verify linux none proc` (**done, linux**) | guest open/read of `/proc/*` via hooks; `/proc/self/{cmdline,environ}` (incl. `PID=`); no host `/tmp/pm_metal_proc` (Phase 6b) |

---

## Done when

- [x] `mount/` module lands; single-root behavior byte-for-byte identical to today (Phase 1)
- [x] `/etc/fstab` parsed + applied at boot, missing file is a no-op (Phase 2)
- [x] `--mount=` CLI flag, linux-only, equivalent to a synthetic fstab line (Phase 2)
- [x] `tmpfs` fstype works on linux (`/dev/shm`-backed) (Phase 3)
- [ ] `device.h` (`RAMDISK` kind) lands on zephyr as its own reusable primitive, not buried in `tmpfs.c` (Phase 3)
- [ ] `tmpfs` fstype works on zephyr (real ram-disk + `fs_mount()`) — blocked on `wasi/file.c` (Phase 3)
- [x] two independently-named `tmpfs` sources coexist without clobbering each other; a repeated name reuses instead of re-creating (Phase 3, linux)
- [x] boot-time populate: ustar [+ lz4] embeds register into a global list; `populate_all()` extracts against guest `/` via resolve + port write (Phase 4, linux)
- [x] guest-callable `mount()`/`umount()`, privileged-only, proven by a mod sequence (Phase 5)
- [x] Phase 6a interim materialization deleted (replaced by 6b)
- [x] Phase 6b: virtual `proc` — open/read answered by hooks; no host backing dir; `/proc/self/{cmdline,environ}` per process (**linux**)
- [x] Phase 6b: Metal WASI path intercept for `proc` on linux (`wasi/file.c` + `posix_file_real.c`)
- [x] Phase 6c: live remount on linux — path ops re-resolve against the live table; same-process `mount()` visible
- [ ] Phase 6b/6c on zephyr: same virtual-proc + live-remount contract once `wasi/file.c` is real
- [x] WASI prestat freeze + linux live-remount fix documented here
