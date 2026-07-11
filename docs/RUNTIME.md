# Runtime

Fundamental process model. Not one-shot — **long-lived host with dynamic load/unload**.

See [LAYERS.md](LAYERS.md) · [SOURCETREE.md](SOURCETREE.md).

---

## Model

One native binary per target. Boots once, stays up, loads many `.wasm` modules over its lifetime.

```
boot
  port + WAMR init (pool from memory budget)
  mount VFS root (one tree, same for every load)
loop
  load(path | bytes) → handle
  run(handle, argv, env?) → exit code   # same VFS root as every other load
  unload(handle)
shutdown
  WAMR destroy
```

Linux may expose the loop via CLI, stdin, or socket — **same API** on every target. Zephyr `main` calls the same functions; no special one-shot path.

---

## VFS root (same every load)

Set once at `init`. Every `load`/`run` sees the **same** tree. No per-mod remapping, no guest path hiding.

| Config | Meaning |
|--------|---------|
| `memory_bytes` | WAMR pool budget — **linux:** sole source (CLI/`malloc`); **zephyr:** optional cap on probed remainder |
| `vfs_root` | host-side root of the guest filesystem |

Guest path = path under `vfs_root`. Preopen that directory as WASI `/`. Guest opens `/foo/bar` → host `vfs_root/foo/bar`. **1:1 — what you see in wasm is what is on disk under root.**

| Target | `vfs_root` backing | Bring-up |
|--------|-------------------|----------|
| **linux** | normal host directory | pass path at `init` (CLI/config) |
| **zephyr** | FAT ramdisk (or image) mounted as rootfs | build/populate image, mount `/`, pass mount point at `init` |

`.wasm` files live **inside** the tree (e.g. `/mods/t0_hello.wasm`) or are loaded via `load_bytes` — VFS root is still the same preopen for WASI file I/O.

---

## API (`runtime.h`)

```c
typedef struct pm_metal_runtime_config {
	uint64_t memory_bytes;   /* WAMR pool budget */
	const char *vfs_root;    /* preopened as guest / — same for all loads */
} pm_metal_runtime_config_t;

/* impl: common — lifecycle */
int pm_metal_runtime_init(const pm_metal_runtime_config_t *cfg);
int pm_metal_runtime_shutdown(void);

/* impl: common — dynamic loader */
int pm_metal_runtime_load_file(const char *path, pm_metal_runtime_handle_t *out);
int pm_metal_runtime_load_bytes(const uint8_t *wasm, uint32_t len,
				pm_metal_runtime_handle_t *out);
int pm_metal_runtime_run(pm_metal_runtime_handle_t h, int argc, char **argv);
int pm_metal_runtime_unload(pm_metal_runtime_handle_t h);
```

WASI `dir_list` at instantiate: `[vfs_root]` → guest `/`. Set in `init`, reused on every `load` — not per-handle overrides.

| Phase | WAMR | Memory |
|-------|------|--------|
| `init` | `wasm_runtime_full_init` once | pool from `platform` — lives until `shutdown` |
| `load` | `load` + `instantiate` | per-instance stack/heap; guest linear memory |
| `run` | `execute_main` | guest uses WASI + linear mem |
| `unload` | `deinstantiate` + `unload` | instance freed; pool remains |
| `shutdown` | `wasm_runtime_destroy` | pool released |

No `run_file()` shortcut that hides load/unload — callers use the loop explicitly.

---

## What the binary expects at `init`

| Field | linux | zephyr |
|-------|-------|--------|
| `memory_bytes` | WAMR pool size — **given** at init (`malloc`/`mmap`) | optional cap; default = probed tail after kernel static + heap |
| `vfs_root` | host dir path | mounted FAT ramdisk path (e.g. `/`) |

Then: load → run → unload (any number of mods). VFS config does not change between loads.

`main.c`: build/populate vfs (zephyr: ramdisk image) → `init(cfg)` → loader loop → `shutdown`.

---

## Later — HTTPS populate

`vfs_root` mount stays required. HTTPS does **not** replace the root — it fills it.

```
load("/mods/foo.wasm")
  → in vfs_root?  yes → load + instantiate
  → missing?      fetch https (cert check) → write into vfs_root → load + instantiate
  → run
```

| Piece | Where |
|-------|--------|
| mounted root | always — dir (linux) or ramdisk (zephyr) |
| cert policy | platform / plat-private fetch layer |
| cache | bytes land under `vfs_root` before WAMR sees them |
| guest | unchanged — still opens `/mods/foo.wasm` on `/` |

Same loader API; fetch is an implementation detail inside `load_file` (or a bind helper it calls).

---

## Later — mod loads mod (virtual `/sys/loader`)

Guests cannot call `pm_metal_runtime_*` directly. Expose the **same** loader through synthetic VFS nodes — not files on disk, implemented by the host WASI layer alongside real `vfs_root` paths.

```
/sys/loader/load      write: "/mods/bar.wasm\n"     read: "handle=3\n"
/sys/loader/run       write: "3\n"                  read: exit code (blocking)
/sys/loader/unload    write: "3\n"
```

Guest C equivalent of shell `echo /mods/bar.wasm > /sys/loader/load`:

```c
int fd = open("/sys/loader/load", O_WRONLY);
write(fd, "/mods/bar.wasm\n", …);
close(fd);
/* host: load_file → store handle in instance table */
```

| Layer | Role |
|-------|------|
| native `runtime.c` | owns WAMR load/run/unload |
| virtual `/sys/*` | guest control plane — write path → host calls runtime API |
| real vfs_root | `.wasm` bytes + guest data files — 1:1 as today |

Parent mod stays running; child is a **new instance** (new handle, own linear memory). Same `vfs_root` preopen for every instance. Policy later: which paths a mod may request, max depth, HTTPS populate on miss.

Implement in WASI file backend (`src/zephyr/…/file.c`, linux wrapper when needed) — not in guest headers until mod-facing API is defined.

**Backend vs guest headers**

| Side | Headers | Style |
|------|---------|--------|
| **host** (`src/`) | `runtime.h`, `platform.h`, plat-private `wasi/file.h` | procedural — `#include` + call `pm_metal_runtime_*`, `pm_metal_port_*` |
| **guest** (mods) | none required; `include/metal.h` later optional | WASI `open`/`write` on `/sys/loader/*` is the floor |

WASI shim does **not** reimplement loader logic — it parses the virtual write and calls the same `runtime.h` symbols `main` uses. Deeper still, `runtime.c` calls `platform.h` for vfs, memory, fetch-on-miss, etc. Host tree only; never linked into wasm.

---

## Bring-up plan

Scaffold is in place (`src/`, `mods/`, `scripts/setup-ide.sh`). Order:

### 1. Toolchain + scripts

| Script | Purpose |
|--------|---------|
| `setup-west.sh` | `west update` → `external/wamr`, `external/zephyr` |
| `setup-tools.sh` | wasi-sdk, pinned toolchain under `.tools/` |
| `build-linux.sh` | WAMR + link `pm-linux-runtime` |
| `build-mod.sh` | `mods/*` → `build/mods/*.wasm` |
| `verify-linux.sh` | init → load → run → unload → shutdown |

### 2. Linux — first green path

**Memory (linux):** only the **given** WAMR budget matters — `memory_bytes` on `pm_metal_runtime_init()` (CLI/argv). Host `malloc`s (or `mmap`s) that pool; no machine probe, no kernel static/heap math. `pm_metal_port_machine_ram()` is optional/deferred on linux (not on the bring-up critical path).

1. **`runtime.c` (common)** — WAMR: `init` takes `memory_bytes` → establish pool; `load_file` / `load_bytes` / `run` / `unload` / `shutdown`; WASI preopen `vfs_root` → guest `/`.
2. **`linux/main.c`** — CLI: `memory_bytes`, `vfs_root`, wasm path; call the loader loop.
3. **`platform.c` (linux)** — bind stubs only for now (`machine_ram` later if needed for reporting).

Proof: `t0_hello.wasm` prints hello from a host-dir vfs root.

### 3. WASI T1 on linux

Fixture file under vfs root → `t1_read.wasm` → confirms 1:1 guest paths.

### 4. Two mods, one process

Load `t0_hello` then `t1_read` without restart — shared `vfs_root`, handle table.

### 5. Zephyr (after linux green)

**Memory (zephyr):** kernel **static** (link image through `&_end`) and kernel **heap** (`CONFIG_HEAP_MEM_POOL_SIZE`, in-link `k_malloc` pool) are accounted first. Port **probes** total installed RAM, subtracts kernel footprint → **remainder → WAMR pool** (map past `_end` on MMU targets, static cap on native_sim). Probe samples: `backup/2nd_try/host/zephyr/pymergetic/metal/port/plat.c` (E820/`x86_memmap` on qemu, `CONFIG_SRAM_SIZE` fallback on native_sim).

| Piece | Source |
|-------|--------|
| `machine_ram` | probe — multiboot E820 / devicetree / `CONFIG_SRAM` per profile |
| `link_used` | `&_end` − `CONFIG_SRAM_BASE_ADDRESS` |
| `kernel_heap` | `CONFIG_HEAP_MEM_POOL_SIZE` (not given to WAMR) |
| WAMR pool | `arena_budget` ≈ `machine_ram − link_used` (page-rounded), via `pm_metal_port_wamr_pool_establish()` |

`pm_metal_runtime_init()` on zephyr may ignore or cap `memory_bytes` against probed `arena_budget` — detail at implement time.

Build path: `build-zephyr-native-sim.sh` → **`platform.c` (zephyr)** probe + pool establish → `wasi/file.c` → FAT ramdisk overlay → same mods on native_sim, then qemu.

### 6. Later (non-blocking)

Virtual `/sys/loader` · `include/metal.h` convenience · HTTPS fetch-on-miss · merged `compile_commands` for IDE.

---

## Done when

- [ ] `init(memory, vfs_root)` → load → run → unload → `shutdown` on linux
- [ ] zephyr: FAT ramdisk mounted, same `vfs_root` semantics
- [ ] guest `/path` = file under `vfs_root/path` on both (no remapping)
- [ ] two mods in one process, same vfs_root for both
- [ ] WASI T1 read works on both
- [ ] `verify` uses dynamic loader API
