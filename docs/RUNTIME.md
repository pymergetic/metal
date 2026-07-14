# Runtime

Fundamental process model. Not one-shot — **long-lived host with dynamic load/unload**.

See [LAYERS.md](LAYERS.md) · [SOURCETREE.md](SOURCETREE.md) · [CONSOLE.md](CONSOLE.md).

---

## Model

One native binary per target. Boots once, stays up, loads many `.wasm` modules over its lifetime.

```
boot
  port + WAMR init (pool from memory budget)
  mount VFS root (one tree, same for every load)
loop
  load(path | bytes) → handle
  spawn(handle, argv, env) → pid        # runtime/process.h — decoupled from handle,
  wait(pid) → exit code                 # same handle may have several pids at once
  unload(handle)                        # refused while any pid against it is still running
shutdown
  WAMR destroy
```

`run()`/`run_ex()` (below) are the low-level, synchronous-only primitive `runtime.c` itself exposes — one call, one execution, blocks until it returns. `runtime/process.h`'s `spawn()`/`wait()`/`try_wait()`/`list()` is the layer built on top that every actual caller (the shell's `run`, its wasm-override dispatch, and scripted mode alike) uses instead: it puts that one execution on its own background worker and hands back a `pid`, so the *same* handle can have several executions in flight at once, each independently trackable — see "Processes" below.

Linux may expose the loop via CLI, stdin, or socket — **same API** on every target. Zephyr `main` calls the same functions; no special one-shot path.

---

## VFS root (same every load)

Set once at `init`. Every `load`/`run` sees the **same** tree. No per-mod remapping, no guest path hiding.

| Config | Meaning |
|--------|---------|
| `memory_bytes` | kheap pool budget — **linux:** sole source (CLI/`malloc`); **zephyr:** optional cap on probed remainder |
| `bytecode_bytes` | bytecode arena budget — raw `.wasm` module buffers, separate pool from `memory_bytes` |
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
	uint64_t memory_bytes;    /* WAMR pool budget */
	uint64_t bytecode_bytes;  /* bytecode arena budget — separate pool, see below */
	const char *vfs_root;     /* preopened as guest / — same for all loads */
} pm_metal_runtime_config_t;

/* impl: common — lifecycle */
int pm_metal_runtime_init(const pm_metal_runtime_config_t *cfg);
int pm_metal_runtime_shutdown(void);

/* impl: common — dynamic loader */
int pm_metal_runtime_load_file(const char *path, pm_metal_runtime_handle_t *out);
int pm_metal_runtime_load_bytes(const uint8_t *wasm, uint32_t len,
				pm_metal_runtime_handle_t *out);
int pm_metal_runtime_run(pm_metal_runtime_handle_t h, int argc, char **argv);
int pm_metal_runtime_run_ex(pm_metal_runtime_handle_t h, int argc, char **argv, int envc, const char **envp,
			     int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd, uint32_t custom_tag);
int pm_metal_runtime_unload(pm_metal_runtime_handle_t h);
```

`run()` is `run_ex()` with `0, NULL, -1, -1, -1` — see "Console model" below for what the `-1`s mean and the per-handle-console extension point `run_ex()` exists for, and "Processes" below for `envc`/`envp`.

`memory_bytes` is a *request*; `runtime_init()` never touches memory itself — it hands the request to an ops table and gets a pool back. Memory (probe + the two pools below) lives in `pymergetic/metal/memory/`, not `platform.h` — three small modules, each a contract header declaring one struct-of-function-pointers and one `bind` getter that returns it. All three share the *same* struct layout, `pm_metal_memory_ops_t`, defined once in `memory/ops.h` alongside a `pm_metal_memory_kind_t` enum and a `pm_metal_memory_resolve(kind)` lookup for callers that pick a kind dynamically; `ram.h`/`kheap.h`/`bytecode.h` each `#include` it and declare their own dedicated getter for the common case where the call site already knows its kind at compile time (see docs/SOURCETREE.md "Ops-struct flavor of `bind`" for the pattern and why: these three are always used together and always come from the one target implementation linked into the binary, so grouping each one's functions beats one symbol per function — at effectively zero cost, since the getter is resolved at build/link time and the indirect calls through the table happen a handful of times per mod load, nowhere near a hot path):

```c
/* src/common/pymergetic/metal/memory/ops.h — the one shared layout + kind enum */
typedef enum pm_metal_memory_kind {
	PM_METAL_MEMORY_RAM = 0,
	PM_METAL_MEMORY_KHEAP,
	PM_METAL_MEMORY_BYTECODE,
	PM_METAL_MEMORY_KIND_COUNT,
} pm_metal_memory_kind_t;

typedef struct pm_metal_memory_ops {
	uint64_t (*probe)(void);
	void *(*establish)(uint64_t requested_bytes, uint64_t *out_bytes);
	void (*release)(void);
	uint64_t (*bytes)(void);
	void *(*alloc)(uint32_t size);
	void (*free)(void *ptr);
} pm_metal_memory_ops_t;

const pm_metal_memory_ops_t *pm_metal_memory_resolve(pm_metal_memory_kind_t kind); /* impl: common */

/* src/common/pymergetic/metal/memory/ram.h */
const pm_metal_memory_ops_t *pm_metal_memory_ram_ops(void); /* impl: bind */

/* src/common/pymergetic/metal/memory/kheap.h */
const pm_metal_memory_ops_t *pm_metal_memory_kheap_ops(void); /* impl: bind */
```

`pm_metal_memory_resolve()` has exactly one implementation (`memory/ops.c`, `impl: common`, not per-target) — it just `switch`es on `kind` and forwards to the matching dedicated getter, so runtime.c/main.c still call `pm_metal_memory_kheap_ops()`/`bytecode_ops()`/`ram_ops()` directly wherever the kind is already known; `resolve()` exists for the rarer case of picking a kind at runtime.

Each module's ops table leaves the slots it has no use for `NULL` (`ram`'s table only sets `.probe`; `kheap`'s only sets `.establish`/`.release`/`.bytes`) — `NULL` here means "not applicable to this module," never "not implemented yet" (a target that hasn't implemented a slot its module *does* use, like zephyr's kheap/bytecode today, still assigns a real stub function that returns `0`/`NULL`, so callers never null-check before calling a slot their module is documented to support).

Two different "how much RAM" numbers, kept as two different modules — never conflated, and **runtime.c never allocates memory itself**: `ram_ops()->probe()` is always **real total machine RAM**, the same meaning on every target. `kheap_ops()->establish()`/`bytes()` is **what WAMR actually got** — on linux, `establish()` is a plain `malloc(requested_bytes)` echoing the request back verbatim; on zephyr it will probe `ram_ops()->probe()`, subtract kernel static + heap, and establish from the remainder — capping or ignoring the request (see Bring-up plan §5). The two are never equal on linux and must not be derived from one another there. `runtime_init()` calls `establish()` and wires the returned pointer into WAMR's `RuntimeInitArgs`; `runtime_shutdown()` calls `release()` — the *only* place pool memory is decided or freed is the target's `memory/kheap.c`.

A **third** pool, sized by `bytecode_bytes` and completely separate from the kheap pool above, holds raw `.wasm` module bytes — the buffers `load_file()`/`load_bytes()` hand to `wasm_runtime_load()` and that stay alive until `unload()`:

```c
/* src/common/pymergetic/metal/memory/bytecode.h */
const pm_metal_memory_ops_t *pm_metal_memory_bytecode_ops(void); /* impl: bind */
```

Why a *third* pool instead of just `malloc`ing bytecode buffers, or folding them into the kheap pool: on linux `malloc` is effectively unlimited so it would not matter — but on zephyr **everything** not inside the kheap pool lands in the kernel heap, which is small, fixed, and shared with the rest of the OS. Handing WAMR's own `Alloc_With_Pool` allocator a mix of "its own bookkeeping + guest linear memory" *and* "arbitrarily large raw mod files" makes the one pool's sizing unpredictable — a big mod could starve WAMR's internal structs. A dedicated, separately-budgeted arena keeps the two failure modes apart and gives an explicit, testable "no room for this mod" error instead of a WAMR-internal allocation failure deep in `wasm_runtime_load()`. `bytecode_ops()`'s `establish()`/`release()`/`bytes()` mirror the kheap pool's shape exactly (it's the same struct layout, one arena per process, same init/shutdown enforcement); `alloc()`/`free()` are the sub-allocators `load_file()`/`load_bytes()`/`unload()` use per mod, and the slots `kheap_ops()` leaves `NULL`. linux backs the arena with `malloc(requested_bytes)` carved up by a small first-fit, coalescing free-list allocator (`pymergetic/metal/util/arena.h`, `impl: shared` — pure C, no OS dependency, so the same code will back zephyr's slice of kernel heap unchanged); zephyr (later) draws this from the *same* `arena_budget` remainder the kheap pool is sized from (see Bring-up plan §5) — the two pools are two slices of one probed budget, not two independent probes.

Same "runtime.c never touches the raw resource itself" rule for reading mod bytecode off storage — this one stays a plain per-function `bind` symbol in `platform.h` (not an ops struct — it stands alone):

```c
/* src/common/pymergetic/metal/port/platform.h */

/* impl: bind */
int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len);
```

`load_file()`'s guest-style path (e.g. `/mods/foo.wasm`) is resolved against `vfs_root` by plain string concat *in* `runtime.c` — that part is genuinely OS-independent. But turning a resolved host path into bytes is not: linux backs it with `fopen`/`fread` into a buffer from `pm_metal_memory_bytecode_ops()->alloc()`; zephyr (later) will back it with `fs_open()`/`fs_read()` (`CONFIG_FILE_SYSTEM`) instead, since bare libc stdio is not guaranteed to reach mounted storage on embedded targets. Keeping the actual read (and the allocation it reads into) behind the port means `load_file()` does not change at all when zephyr lands.

WASI preopen at instantiate: guest `/` must come from WAMR `map_dir_list` (format `<guest>::<host>`), not plain `dir_list` — `dir_list` preopens under the *same* string on both sides, and `vfs_root` is a host-absolute path while the guest wants `/`. So the one map entry, built once at `init` and reused on every `run`, is `"/::" + vfs_root` (`vfs_root` must be absolute).

WASI args (dir/map-dir/argv/env) are only consumed by WAMR at `wasm_runtime_instantiate()` — set on the *module*, baked into the instance at that call. Since this API takes `argv` on `run()`, after `load()` already returned a handle, `load()` cannot instantiate up front: it only `wasm_runtime_load()`s (parse) and keeps the byte buffer alive. `run()` sets WASI args for that call, instantiates, executes `main`, then deinstantiates — so every `run()` gets a fresh instance (and may pass different `argv`), while the parsed module + buffer persist across runs until `unload`.

| Phase | WAMR | Memory |
|-------|------|--------|
| `init` | `wasm_runtime_full_init` once | kheap pool + bytecode arena from `memory/` ops — both live until `shutdown` |
| `load` | `load` only (parse; buffer kept, not yet freed) | buffer from the bytecode arena, owned by the runtime until `unload` |
| `run` | `set_wasi_args` + `instantiate` + `execute_main` + `deinstantiate` | per-call stack/heap from the kheap pool; guest linear memory freed when the call returns |
| `unload` | `unload` | buffer freed back to the bytecode arena; pools remain |
| `shutdown` | `wasm_runtime_destroy` | both pools released |

No `run_file()` shortcut that hides load/unload — callers use the loop explicitly. `run()` may be called more than once per handle (e.g. same mod, different `argv`); `load`/`unload` still bracket it exactly once each.

---

## Console model

Full design + implementation status: **[CONSOLE.md](CONSOLE.md)**. Short
version, from `runtime.c`'s side only:

- **The runtime's own diagnostics** (`fprintf(stderr, "pm_metal_runtime: ...")`) stay plain host-side C, unaffected by any of this — `runtime.c` itself never calls into `console.h`/`viewport.h`, and does not need to: those live one layer up, in `app/app.h`'s console-mode runner.
- **Each loaded module's own WASI stdio (fd 0/1/2)** goes through `run_ex()` — the one seam `runtime.c` exposes for this:

```c
int pm_metal_runtime_run_ex(pm_metal_runtime_handle_t h, int argc, char **argv, int envc, const char **envp,
			     int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd, uint32_t custom_tag);
```

`run()` is `run_ex()` with `-1,-1,-1` for the fds and `0` for `custom_tag` — WAMR's own sentinel for "inherit the host process's real fd 0/1/2". `run_ex()` forwards whatever it's given straight to `wasm_runtime_set_wasi_args_ex()`; `runtime.c` does not know or care whether those fds are real terminal fds or one end of a per-handle console pipe. `src/linux/main.c`'s `--console` mode is the reference caller (per-handle sinks + a switchable-focus viewport onto a real terminal) — see [CONSOLE.md](CONSOLE.md) for the full sink/viewport/log-level design and `scripts/verify-linux-console.sh` for the proof. The console-mode run loop itself (kernel sink, `PM_METAL_VIEWPORT_LOCAL` pump loop, dispatcher thread) is `impl: common`, in `pymergetic/metal/app/app.h`'s `pm_metal_app_run_console()` — `src/linux/main.c` only does argv parsing and one call into it, so any future target's own `main.c` reaches the same console mode without reimplementing it (see docs/SOURCETREE.md's `app/` entry). Command *dispatch* itself (turning a typed line into a `load`/`run`/`unload`/... call, or a `.wasm` override's own execution) lives one layer further down, in `pymergetic/metal/shell/` (`impl: common`, see [CONSOLE.md](CONSOLE.md) "Shell") — `app.c` only owns the kernel sink and the pump/dispatch-thread handshake, not the command set.

---

## Processes (`runtime/process.h`)

`runtime.c`'s own `run()`/`run_ex()` are synchronous: one call, one execution, on the calling thread. Every real caller wants something else — a background execution it can check on or wait for later — and, now, more than one execution of the *same* handle at once (e.g. `run 3` twice before the first finishes). `runtime/process.h` is that layer, built entirely on top of `runtime.h`'s own public API (`run_ex()` plus the `refcount` `runtime.c` already keeps per handle — see "Concurrency" below) — it adds no new synchronization *inside* the runtime itself:

```c
typedef struct pm_metal_process_id { uint32_t pid; } pm_metal_process_id_t;

int pm_metal_process_init(void);
void pm_metal_process_shutdown(void);

int pm_metal_process_spawn(pm_metal_runtime_handle_t handle, int argc, char **argv, int envc, const char **envp,
			    int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd, FILE *guest_out,
			    pm_metal_process_exit_cb on_exit, void *on_exit_ctx, pm_metal_process_id_t *out_pid);
FILE *pm_metal_process_guest_out(pm_metal_process_id_t pid);
int pm_metal_process_try_wait(pm_metal_process_id_t pid, int *out_exit_code);
int pm_metal_process_wait(pm_metal_process_id_t pid, int *out_exit_code);
void pm_metal_process_list(void (*visit)(const pm_metal_process_info_t *info, void *ctx), void *ctx);
```

A **handle** (`runtime.h`, `1..PM_METAL_RUNTIME_MAX_HANDLES`) is one loaded module; a **process** (`process.h`, `pm_metal_process_id_t`, `1..PM_METAL_PROCESS_MAX`) is one execution of it, on its own background worker (`port/worker.h`) — same relationship a real OS has between a binary on disk and its N currently-running instances. `spawn()` copies `argv`/`envp` itself before returning (the caller's own arrays, e.g. a tokenized command line, need not outlive the call, unlike `run_ex()`'s), starts the worker, and hands back a `pid` immediately; `wait()`/`try_wait()` reap it (blocking / non-blocking) once it finishes; `list()` is read-only, for a `ps`-style command. `on_exit` (optional) fires once, on the process's own worker thread, the instant it finishes — the shell's `run` builtin uses it to log `exit=%d` onto the handle's own console pane live, without a poll loop; a caller that's going to `wait()` the pid itself right away (e.g. the shell's synchronous wasm-override dispatch) passes `NULL` instead.

Env vars are real WASI env (`run_ex()`'s `envc`/`envp`, forwarded to `wasm_runtime_set_wasi_args_ex()`) — nothing to do with the host's own `environ`. The shell keeps one small `export`ed set per console (`pm_metal_shell_ctx_t.env`, see [CONSOLE.md](CONSOLE.md) "Shell"), snapshotted into `spawn()`'s `envp` on every `run`/wasm-override dispatch; a bare `run 3` with nothing exported passes `envc=0, envp=NULL`, identical to calling `run()` directly.

`pm_metal_process_shutdown()` must run **before** any handle-table teardown (`shell/commands.c`'s `handles_shutdown()`, then `pm_metal_runtime_shutdown()`) — it blocks until every in-flight process has actually finished, so no handle's `refcount` is still held above zero when `unload()`/`runtime_shutdown()`'s own unload loop runs. Both of `app/app.c`'s run modes (console mode and scripted mode) call it in that order — see [CONSOLE.md](CONSOLE.md) "Shell" and `commands.h`'s own doc comment.

`run_ex()` also tags every instance it successfully instantiates with a caller-supplied `custom_tag`, via `wasm_runtime_set_custom_data()`, before `execute_main()` runs — generic per-instance metadata `runtime.c` sets unconditionally (opaque to `runtime.c` itself; `run()` always passes `0`), not a shell-specific concept, but the one thing that lets a native function a guest calls *from inside* `execute_main()` (`wasm_runtime_get_custom_data()` on its own `wasm_runtime_get_module_inst()`) find its way back to whatever the caller cares about. `spawn()` above is the one real user: it passes its own `pid` as `custom_tag`, deliberately pid- rather than handle-grained, since the same handle can have several processes in flight at once and it's specifically *this* execution's own state that matters. The one consumer today is [CONSOLE.md](CONSOLE.md)'s "Guest-callable commands" (`shell/guest_exec.c`), which resolves that pid via `pm_metal_process_guest_out()` to the `FILE*` this process's own output should land on (a console sink's `->out` for console `run`/wasm-override dispatch, plain host `stdout` for scripted mode — see each of `spawn()`'s three call sites) — nothing in `runtime.c` or `process.c` itself depends on shell/console at all; `guest_out` is just an opaque `FILE*` to both.

---

## Concurrency

`init()`/`shutdown()` are **controller-thread-only**: call each exactly once, with no other `pm_metal_runtime_*()` call in flight — the same rule WAMR itself uses for `wasm_runtime_full_init()`/`wasm_runtime_destroy()`. In practice: call `init()` before spawning any worker threads, and only call `shutdown()` after joining all of them.

Once `init()` has returned, `load_file()`/`load_bytes()`/`run()`/`unload()` are safe to call **concurrently from multiple threads**, including on different handles at the same time — one process-wide mutex (`g_pm_metal_runtime_lock`, `port/lock.h`'s `pm_metal_port_mutex_t`) in `runtime.c` guards:

- the shared handle table (claiming a slot in `load_*()`, disowning it in `unload()`, the busy `refcount` used to reject `unload()` while a `run()` on that handle is in flight — it returns `-1` rather than racing the module out from under a running instance; the caller must retry),
- and — this is the important part — **every call into `wasm_runtime_load()`/`instantiate()`/`deinstantiate()`/`unload()`**, full stop, even across different handles/modules.

`hold()`/`release()` bump/drop that same `refcount` directly, with no `run_ex()` call of their own — `runtime/process.h`'s `spawn()` is the one caller: it starts a process on its own background worker thread and returns immediately, *before* that thread is guaranteed to have run far enough to reach `run_ex()`'s own `refcount++`. Without a synchronous `hold()` taken at `spawn()` time (dropped by that worker once its `run_ex()` call returns), `unload()` could see a just-spawned-but-not-yet-scheduled process's handle as idle and free the module out from under the thread about to use it — a real, easily-reproduced race (typing `run 1` immediately followed by `unload 1` at the console prompt hit it on nearly every attempt before this fix). Ordinary direct callers of `run()`/`run_ex()` that stay on their own thread the whole time (`thread_stress_test.c`) have no such gap and never call `hold()`/`release()` themselves.

That last point is more conservative than WAMR's own docs suggest. Upstream describes the shared `Alloc_With_Pool` heap (`external/wamr/core/shared/mem-alloc/ems/`) as internally locked, which would imply concurrent `load()`/`instantiate()` on *different* modules from different threads is safe without a caller-side lock. **It isn't, empirically, in this vendored build:** `scripts/verify-linux-threads.sh` builds a pthread-based stress harness (`src/linux/thread_stress_test.c`) with ThreadSanitizer and, before the fix above, TSan caught real data races inside `ems_alloc.c`'s `alloc_hmu()`/`gc_alloc_vo()`/`gc_free_vo()` when `load`/`instantiate`/`deinstantiate`/`unload` ran concurrently on different modules — every one of the four calls hit it, not just one. So this codebase doesn't trust the "pool is internally locked" assumption; it serializes those four calls itself instead.

`wasm_application_execute_main()` — the actual guest bytecode interpretation — showed **no races** in the same run and is deliberately left unlocked, so two different handles' guest code still runs fully in parallel; only the WAMR-internal setup/teardown around it is serialized. `set_wasi_args()` is folded into the same locked section as `instantiate()` for an independent, second reason: it writes onto the *module* (not a future instance), consumed by the very next `instantiate()` call, so the two must stay paired — no other `run()` on the same handle may sneak its own `set_wasi_args()` in between, or one call's `argv` could end up baked into the other's instance.

The mutex primitive itself (`init`/`destroy`/`lock`/`unlock`) is a `bind` port module (`port/lock.h`, `impl: bind` in `src/linux/.../port/lock.c` and `src/zephyr/.../port/lock.c`) — `pthread_mutex_t` on linux, `struct k_mutex` on zephyr, reinterpreted in place from a fixed-size opaque buffer so the shared header never `#include`s an OS header. `runtime.c` inits it right before publishing `initialized = 1` in `init()` and destroys it in `shutdown()`, so repeated `init()`→`shutdown()`→`init()` cycles never re-init a live mutex. `memory/bytecode.c`'s arena (`pymergetic/metal/util/arena.h`, no locking of its own) gets its own separate `pm_metal_port_mutex_t` around `alloc()`/`free()`, following the same pattern — see the comment there — since concurrent `load()`/`unload()` calls on different handles mutate the same free-list.

Reproduce: `scripts/verify-linux-threads.sh` — builds `pm-linux-thread-stress` and `pm-wamr-vmlib` with `-fsanitize=thread` (and `WAMR_DISABLE_HW_BOUND_CHECK=1`, since WAMR's default hardware-bounds-check guard-page `mmap` and TSan's shadow-memory layout don't get along — unrelated to the locking itself), runs it under `setarch -R` (ASLR off — this host's TSan build aborts at startup under full ASLR; also unrelated to the locking), and fails if TSan reports anything.

---

## What the binary expects at `init`

| Field | linux | zephyr |
|-------|-------|--------|
| `memory_bytes` | kheap pool size — **given** at init (`malloc`/`mmap`) | optional cap; default = a share of the probed tail after kernel static + heap |
| `bytecode_bytes` | bytecode arena size — **given** at init (`malloc`) | optional cap; default = the remaining share of that same probed tail |
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
| `verify-linux-threads.sh` | ThreadSanitizer proof of the "Concurrency" section above — `pm-linux-thread-stress` |
| `verify-linux-console.sh` | `--console` mode: load/run/focus/escape/unload/quit — see [CONSOLE.md](CONSOLE.md) |

### 2. Linux — first green path — done

**Memory (linux):** only the **given** kheap budget matters — `memory_bytes` on `pm_metal_runtime_init()` (CLI/argv), handed to `pm_metal_memory_kheap_ops()->establish()`, which `malloc`s it verbatim (`Alloc_With_Pool`); no machine probe, no kernel static/heap math feeds pool sizing. `pm_metal_memory_ram_ops()->probe()` *is* a real probe on linux (`sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE)`) — diagnostics/reporting only; unlike zephyr it is not wired into pool sizing, since the process shares the host with everything else and does not own all of it.

1. **`runtime.c` (common)** — WAMR: `init` takes `memory_bytes` + `bytecode_bytes` → calls `pm_metal_memory_kheap_ops()->establish()` for the kheap pool and `pm_metal_memory_bytecode_ops()->establish()` for the bytecode arena (never allocates either directly); `load_file` / `load_bytes` allocate from the bytecode arena via `bytecode_ops()->alloc()` and keep the buffer; `run` sets WASI args + instantiates + executes `main` + deinstantiates; `unload` unloads + frees the buffer back via `bytecode_ops()->free()`; `shutdown` unloads any live handles, destroys the runtime, releases both pools. One `wasm_runtime_full_init()`/`wasm_runtime_destroy()` pair per process — a second `init()` without an intervening `shutdown()` is rejected.
2. **`linux/main.c`** — CLI: `--memory=<bytes> --bytecode-memory=<bytes> --vfs-root=<dir> /mod.wasm [/mod2.wasm ...]`; resolves `vfs_root` to an absolute path (`realpath`), then loads/runs/unloads each positional `.wasm` in turn inside one `init`/`shutdown` pair. Positional paths are guest-style, not host paths — `load_file()` resolves them against `vfs_root`.
3. **`memory/{ram,kheap,bytecode}.c` (linux)** — `ram`: real `machine_ram` probe. `kheap`: `establish`/`release`/`bytes` = `malloc`/`free` of exactly the requested size. `bytecode`: `establish`/`release`/`bytes`/`alloc`/`free` = `malloc`/`free` of the requested size, carved up by `pymergetic/metal/util/arena.h`. **`port/platform.c` (linux)** — `read_file` = `fopen`/`fread`/`fclose` into a buffer from `pm_metal_memory_bytecode_ops()->alloc()`.

Proof: `scripts/verify-linux.sh` — `t0_hello.wasm` prints hello from a host-dir vfs root.

### 3. WASI T1 on linux — done

Fixture file under vfs root → `t1_read.wasm` → confirms 1:1 guest paths (`scripts/verify-linux.sh` writes `<vfs_root>/README` and checks its content comes back through the guest).

### 4. Two mods, one process — done

`scripts/verify-linux.sh` loads `t0_hello` then `t1_read` in the same `pm-linux-runtime` invocation — shared `vfs_root`, same handle table, no restart between them.

### 5. Zephyr (after linux green)

**Memory (zephyr):** kernel **static** (link image through `&_end`) and kernel **heap** (`CONFIG_HEAP_MEM_POOL_SIZE`, in-link `k_malloc` pool) are accounted first. `memory/ram.c` **probes** total installed RAM, subtracts kernel footprint → **remainder split between `memory/kheap.c` and `memory/bytecode.c`** (map past `_end` on MMU targets, static cap on native_sim). Probe samples: `backup/2nd_try/host/zephyr/pymergetic/metal/port/plat.c` (E820/`x86_memmap` on qemu, `CONFIG_SRAM_SIZE` fallback on native_sim).

| Piece | Source |
|-------|--------|
| `machine_ram` | probe — multiboot E820 / devicetree / `CONFIG_SRAM` per profile (`memory/ram.c`) |
| `link_used` | `&_end` − `CONFIG_SRAM_BASE_ADDRESS` |
| `kernel_heap` | `CONFIG_HEAP_MEM_POOL_SIZE` (not given to WAMR) |
| `arena_budget` | ≈ `machine_ram − link_used` (page-rounded) — split between the two pools below |
| kheap pool | a share of `arena_budget`, via `pm_metal_memory_kheap_ops()->establish()` |
| bytecode arena | the remaining share of the *same* `arena_budget`, via `pm_metal_memory_bytecode_ops()->establish()` |

`pm_metal_runtime_init()` on zephyr may ignore or cap `memory_bytes`/`bytecode_bytes` against probed `arena_budget` — detail at implement time. Both pools drawing from one probed budget (rather than each probing independently) is what keeps the split honest: the firmware only owns `arena_budget` once, not twice.

Build path: `build-zephyr-native-sim.sh` → **`memory/{ram,kheap,bytecode}.c` (zephyr)** probe + pool establish → `wasi/file.c` → FAT ramdisk overlay → same mods on native_sim, then qemu.

### 6. Later (non-blocking)

Virtual `/sys/loader` · `include/metal.h` convenience · HTTPS fetch-on-miss · merged `compile_commands` for IDE.

---

## Done when

- [x] `init(memory, bytecode_memory, vfs_root)` → load → run → unload → `shutdown` on linux
- [ ] zephyr: FAT ramdisk mounted, same `vfs_root` semantics
- [x] guest `/path` = file under `vfs_root/path` on linux (no remapping) — zephyr pending
- [x] two mods in one process, same vfs_root for both — linux; zephyr pending
- [x] WASI T1 read works on linux — zephyr pending
- [x] mod bytecode lives in a dedicated arena, separate from the WAMR pool — linux; zephyr pending (single probed `arena_budget`, split)
- [x] `verify` uses dynamic loader API — `scripts/verify-linux.sh`
- [x] concurrent load/run/unload across handles from multiple threads, proven under ThreadSanitizer — see "Concurrency" above, `scripts/verify-linux-threads.sh`
- [x] per-handle console (own pane, switchable focus, escape back to kernel) — linux, see [CONSOLE.md](CONSOLE.md), `scripts/verify-linux-console.sh`; zephyr pending
- [x] shell command registry + cwd + virtual `/bin/pm` + `.wasm` overrides under `vfs_root/bin/` — linux, see [CONSOLE.md](CONSOLE.md) "Shell", `scripts/verify-linux-console.sh`; zephyr pending (same `impl: common` code, blocked on zephyr's own console/`port/{worker,dir}` binds)
- [x] processes decoupled from handles (`runtime/process.h`, several concurrent `run`s per handle, each own pid) + real WASI env (`export`/`env` builtins, `ps` command) — linux, see "Processes" above, `scripts/verify-linux-console.sh`; zephyr pending
