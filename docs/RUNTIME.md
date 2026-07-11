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

`.wasm` files live **inside** the tree (e.g. `/mods/t0_hello.wasm`) or are loaded via `load_bytes` — VFS root is still the same preopen for WASI file I/O. **`load_file()` enforces this**: its `path` is guest-style and is resolved against `vfs_root` inside `runtime.c` (same rule the `map_dir_list` preopen uses for the guest) — there is no separate "host path" for mod bytecode, so the call is identical whether `vfs_root` is a host dir (linux) or a mounted ramdisk (zephyr). `scripts/verify-linux.sh` copies mods into `<vfs_root>/mods/` and passes `/mods/t0_hello.wasm` etc. as positional args, exactly as it would look on zephyr.

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

`memory_bytes` is a *request*; `runtime_init()` never touches memory itself — it hands the request to the port and gets a pool back:

```c
/* src/common/pymergetic/metal/port/platform.h */

/* impl: bind */
uint64_t pm_metal_port_machine_ram(void);

/* impl: bind */
void *pm_metal_port_wamr_pool_establish(uint64_t requested_bytes, uint64_t *out_bytes);
void pm_metal_port_wamr_pool_release(void);
uint64_t pm_metal_port_wamr_pool_bytes(void);
```

Two different "how much RAM" numbers, both in `platform.h`, kept as two different functions — never conflated, and **runtime.c never allocates memory itself**: `pm_metal_port_machine_ram()` is always **real total machine RAM**, the same meaning on every target. `pm_metal_port_wamr_pool_establish()`/`_bytes()` is **what WAMR actually got** — on linux, `establish()` is a plain `malloc(requested_bytes)` echoing the request back verbatim; on zephyr it will probe `machine_ram()`, subtract kernel static + heap, and establish from the remainder — capping or ignoring the request (see Bring-up plan §5). The two are never equal on linux and must not be derived from one another there. `runtime_init()` calls `establish()` and wires the returned pointer into WAMR's `RuntimeInitArgs`; `runtime_shutdown()` calls `release()` — the *only* place pool memory is decided or freed is the port, per target.

Same rule for reading mod bytecode off storage — **runtime.c never touches stdio itself**:

```c
/* src/common/pymergetic/metal/port/platform.h */

/* impl: bind */
int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len);
```

`load_file()`'s guest-style path (e.g. `/mods/foo.wasm`) is resolved against `vfs_root` by plain string concat *in* `runtime.c` — that part is genuinely OS-independent. But turning a resolved host path into bytes is not: linux backs it with `fopen`/`fread`; zephyr (later) will back it with `fs_open()`/`fs_read()` (`CONFIG_FILE_SYSTEM`) instead, since bare libc stdio is not guaranteed to reach mounted storage on embedded targets. Keeping the actual read behind the port means `load_file()` does not change at all when zephyr lands.

WASI preopen at instantiate: guest `/` must come from WAMR `map_dir_list` (format `<guest>::<host>`), not plain `dir_list` — `dir_list` preopens under the *same* string on both sides, and `vfs_root` is a host-absolute path while the guest wants `/`. So the one map entry, built once at `init` and reused on every `run`, is `"/::" + vfs_root` (`vfs_root` must be absolute).

WASI args (dir/map-dir/argv/env) are only consumed by WAMR at `wasm_runtime_instantiate()` — set on the *module*, baked into the instance at that call. Since this API takes `argv` on `run()`, after `load()` already returned a handle, `load()` cannot instantiate up front: it only `wasm_runtime_load()`s (parse) and keeps the byte buffer alive. `run()` sets WASI args for that call, instantiates, executes `main`, then deinstantiates — so every `run()` gets a fresh instance (and may pass different `argv`), while the parsed module + buffer persist across runs until `unload`.

| Phase | WAMR | Memory |
|-------|------|--------|
| `init` | `wasm_runtime_full_init` once | pool from `platform` — lives until `shutdown` |
| `load` | `load` only (parse; buffer kept, not yet freed) | buffer owned by the runtime until `unload` |
| `run` | `set_wasi_args` + `instantiate` + `execute_main` + `deinstantiate` | per-call stack/heap; guest linear memory freed when the call returns |
| `unload` | `unload` | buffer freed; pool remains |
| `shutdown` | `wasm_runtime_destroy` | pool released |

No `run_file()` shortcut that hides load/unload — callers use the loop explicitly. `run()` may be called more than once per handle (e.g. same mod, different `argv`); `load`/`unload` still bracket it exactly once each.

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

### 1. Toolchain + scripts — done

`external/wamr`, `external/zephyr`, `.tools/wasi-sdk` were already vendored locally — no `setup-west.sh`/`setup-tools.sh` needed for this pass.

| Script | Purpose |
|--------|---------|
| `build-linux.sh` | WAMR (via `src/linux/CMakeLists.txt`) + link `pm-linux-runtime` |
| `build-mod.sh` | `mods/*` → `build/mods/*.wasm` (wasi-sdk, `wasm32-wasip1`) |
| `verify-linux.sh` | build mods + runtime → init → load → run → unload → shutdown |

### 2. Linux — first green path — done

**Memory (linux):** only the **given** WAMR budget matters — `memory_bytes` on `pm_metal_runtime_init()` (CLI/argv), handed to the port's `pm_metal_port_wamr_pool_establish()`, which `malloc`s it verbatim (`Alloc_With_Pool`); no machine probe, no kernel static/heap math feeds pool sizing. `pm_metal_port_machine_ram()` *is* a real probe on linux (`sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE)`) — diagnostics/reporting only; unlike zephyr it is not wired into pool sizing, since the process shares the host with everything else and does not own all of it.

1. **`runtime.c` (common)** — WAMR: `init` takes `memory_bytes` → calls `pm_metal_port_wamr_pool_establish()` for the pool (never allocates it directly); `load_file` / `load_bytes` parse + keep the buffer; `run` sets WASI args + instantiates + executes `main` + deinstantiates; `unload` unloads + frees the buffer; `shutdown` unloads any live handles, destroys the runtime, calls `pm_metal_port_wamr_pool_release()`. One `wasm_runtime_full_init()`/`wasm_runtime_destroy()` pair per process — a second `init()` without an intervening `shutdown()` is rejected.
2. **`linux/main.c`** — CLI: `--memory=<bytes> --vfs-root=<dir> /mod.wasm [/mod2.wasm ...]`; resolves `vfs_root` to an absolute path (`realpath`), then loads/runs/unloads each positional `.wasm` in turn inside one `init`/`shutdown` pair. Positional paths are guest-style, not host paths — `load_file()` resolves them against `vfs_root`.
3. **`platform.c` (linux)** — real `machine_ram` probe; `wamr_pool_establish`/`_release`/`_bytes` = `malloc`/`free` of exactly the requested size; `read_file` = `fopen`/`fread`/`fclose`.

Proof: `scripts/verify-linux.sh` — `t0_hello.wasm` prints hello from a host-dir vfs root.

### 3. WASI T1 on linux — done

Fixture file under vfs root → `t1_read.wasm` → confirms 1:1 guest paths (`scripts/verify-linux.sh` writes `<vfs_root>/README` and checks its content comes back through the guest).

### 4. Two mods, one process — done

`scripts/verify-linux.sh` loads `t0_hello` then `t1_read` in the same `pm-linux-runtime` invocation — shared `vfs_root`, same handle table, no restart between them.

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

- [x] `init(memory, vfs_root)` → load → run → unload → `shutdown` on linux
- [ ] zephyr: FAT ramdisk mounted, same `vfs_root` semantics
- [x] guest `/path` = file under `vfs_root/path` on linux (no remapping) — zephyr pending
- [x] two mods in one process, same vfs_root for both — linux; zephyr pending
- [x] WASI T1 read works on linux — zephyr pending
- [x] `verify` uses dynamic loader API — `scripts/verify-linux.sh`
