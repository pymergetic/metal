# Mods, functions, commands, process (EFI product model)

**Status:** contract locked. Nestable process-on-task-tree landed; µPy binds
the same registries (`pymergetic.metal.mod.<name>.<func>(...)`, lazy
attribute-resolved, see `docs/MICROPYTHON.md`); multi-focus UI still open —
see [Today vs target](#today-vs-target).

One concept with async/coro: mods register callables; work parks on **normal runners** via [`async.h`](../include/pymergetic/metal/runtime/async/async.h); host ≈ guest.

---

## North star

### Mod lifecycle (loader contract)

A **mod** is loaded code, not a task and not a process.

The loader cares about **exactly two** guest exports:

| Export | When | What it does |
|--------|------|----------------|
| **`pm_metal_mod_on_load`** | after instantiate + deps | register **functions** and **commands**; wire anything else |
| **`pm_metal_mod_on_unload`** | before deinstantiate | tear down registrations / private state |

That is the whole loader ABI. **No other magic names.**

In particular the loader must **not** look for `pm_metal_guest_step`, `main`, or any “stem”. If a mod has a long-running loop, that is just a **function it registered** (usually behind a **command**). The loader does not know or care what those functions are called.

Steps:

1. **Load** wasm/AOT → instantiate  
2. Satisfy **deps** — guest/host may `pm_metal_mod_load` other mods (nested `on_load` ok)  
3. Call **`on_load`** → mod registers funcs/cmds  
4. **Ready** in the registry — no task required for existence  
5. **Resolve once** → `fn_t` / `cmd_t` (fn first + name); `fn_coro` (frame via `coro_alloc` in the step)  
6. **Command** → `fn_process` (process-root task + UI/stdio); nestable under a live process  
7. **Unload**: refuse or wait if open tasks; call **`on_unload`**; drop registry rows; deinstantiate  

### Function vs command

| Registration | What it is | Consumers |
|--------------|------------|-----------|
| **Function** | `func_resolve` → `fn_t` → `fn_coro` / task | other mods, host, **µPy** (`pymergetic.metal.mod`) |
| **Command** | `cmd_resolve` → `cmd_t` (`fn` first + `name`) → `fn_process` | **Shell**; **µPy** (`pmcmd.*`) |

Shell, **other mods**, and **µPy** use the **same** registries / load API.
No second py-only table. **No string lookup on the hot path** after resolve.

### Instances: shared "instance 0" vs fresh per-call

A **mod** is loaded code (`wasm_module_t`) — that part is always shared and
loaded once. What varies is which **instance** (`wasm_module_inst_t` +
`wasm_exec_env_t`, i.e. linear memory/globals/stack) a call runs against.
The mod itself declares which instancing it needs — callers don't have to
know or hardcode it:

```c
int32_t pm_metal_mod_on_load(void) {
  pm_metal_mod_set_capability(PM_METAL_MOD_CAP_MULTI); /* real static state */
  ...
}
```

| Capability | `AUTO` resolves to | Forced `SHARED` | Forced `FRESH` | Pick for |
|------------|---------------------|------------------|-----------------|----------|
| `SINGLE` (default — undeclared mods) | `SHARED` | ok | refused | Stateless mods, or fine with shared/reentrant statics (hello, the test mods) |
| `MULTI` | `FRESH` | ok (e.g. a lightweight library call) | ok | Mods with real static state that must not leak/persist across runs, or that should run several times concurrently (Doom) |

Either way, instance 0 always exists and is what runs `on_load` (func/cmd
registration) — it just isn't necessarily what a `MULTI` mod's *invocations*
run against.

| Mode | Linear memory / statics | Who tears it down |
|------|--------------------------|--------------------|
| **Shared** (instance 0) | The mod's one persistent instance, kept in the mod slot | `pm_metal_mod_unload()` / `pm_metal_mod_reset()` |
| **Fresh** | New, private, own heap/globals; module bytes/code still shared | The process that owns it, on exit (`pm_metal_process_reap`/`release`) |

Why fresh instances exist: a mod with real static state (zone heap, screen
buffer, open files — e.g. Doom) leaves that state dirty after it exits.
Reusing instance 0 for the next invocation reruns stale/partially-torn-down
state, and a clean WASI `proc_exit` leaves WAMR's exception flag stuck, so
the next call on that same instance would bail out immediately. A fresh
instance sidesteps both: each `run doom` gets its own linear memory and
exec_env, so `on_load`'s side effects still happen once against the shared
module, but every process-level invocation starts from a truly cold guest
state — including running **the same mod concurrently** (e.g. two Doom
processes at once), since each has its own instance.

Per-call API (`pm_metal_mod_cmd_invoke` / `fn_process`), opt-in via a
`pm_metal_mod_instance_t instance_mode` (`AUTO`/`SHARED`/`FRESH`, see table
above) plus a `uint32_t flags` word:

- `PM_METAL_MOD_FLAG_AUTO_UNLOAD` — once this call's fresh instance is torn
  down, best-effort unload the whole mod too (drop the compiled module +
  registry rows) instead of leaving it `READY`/resident. Silently refused
  (mod just stays loaded) if anything else still needs it.
- `pm_metal_process_spawn_mod()` (real processes) and the shell's bare
  command path (`ModShellCmdFn`) pass `AUTO` — the mod's own capability
  decides. Boot self-tests and inter-mod library calls force `SHARED`.

Plumbing (no new registry, no second table):

- `pm_metal_wasm_mod_image_instantiate()` / `_deinstantiate()` (`wasm.h`) —
  create/destroy an `inst`+`exec_env` pair against an already-loaded
  `module`, without touching the shared module or its bytes.
- `pm_metal_mod_cmd_t` carries `mod_name`/`export_name` alongside the
  resolved shared `fn`, so `fn_process(FRESH)` can re-resolve the export
  against the fresh instance instead of reusing the shared one.
- `MetalProcessSlot` (`process.c`) owns the fresh image once created;
  `pm_metal_process_reap`/`release` hand it to
  `pm_metal_mod_on_fresh_instance_end()` for teardown (+ the auto-unload
  check) — callers never close it themselves. `pm_metal_process_owned_image()`
  is a read-only peek at it (e.g. for `pm_metal_mod_func_resolve_on()` to
  resolve another export against *this* process's own instance instead of
  instance 0).
- `pm_metal_mod_reset(name)` — soft reset: keep the compiled module, just
  recreate instance 0's `inst`/`exec_env` and rerun `on_load` (no
  refetch/recompile). Same idle guard as `unload()`.

### Process (role on the task tree)

**process = command Extrawurst** (UI/stdio redirect) on a **process-root task**.

- One hierarchy: the task/coro tree. No second process parent tree.  
- Process table = flat index of process-root tasks (`root_task_h`, `parent_id`).  
- Every task carries `proc_id` (itself or ancestor process root).  
- Nested `fn_process` → subprocess (child process-root task + redirect stack).  
- Memory: no special root blob — steps `coro_alloc` / malloc as needed.  

```
proc1 (task) ── taskA ── proc2 (task) ── taskB
```

### Async / runners

- Same author shape on host and guest (`async.h`)  
- Fibers/tasks on the **normal runner pool**  
- Guest coro stamps call-in **by value** (inst/exec_env/step)  
- **Stackless:** anything that survives `await` comes from the **Metal host heap**
  (`coro_alloc` / `pm_metal_mem_alloc`) — not the wasm call stack, not a durable
  wasm heap. Wasm linear mem = statics + short in-step stack; step gets a
  temporary linear alias of the host frame for normal `T*` access. Async I/O
  into that frame uses the durable host copy (`guest_buf_durable`).  

### Memory

| Where | What |
|-------|------|
| Metal TLSF (`pm_metal_mem_alloc` / `free`) | Durable frames, buffers, host + guest |
| Wasm linear memory | Module statics + in-step call stack (+ brief coro alias) |

---

## Today vs target

| Today (must die) | Target |
|------------------|--------|
| ~~Host invents cmd from magic `guest_step`~~ **done** | Mod `on_load` registers funcs/cmds |
| ~~`run doom` steals image + forces reload~~ **done** | many mods READY; cmd → process |
| ~~String `func_invoke` + session Extrawurst~~ **done** | resolve → `fn_t` → `fn_coro` / `fn_process` |
| ~~One process; no nest~~ **done** | nestable procs on task tree |
| ~~Create-time root `1024` state~~ **done** | `coro_alloc` in step |
| ~~One shared instance for everything → can't restart/re-run a mod with static state~~ **done** | mod declares `SINGLE`/`MULTI` capability; per-call fresh instance opt-in |
| Prefer-wasm policy for AOT faults | AOT first-class; fix faults |
| Single UI focus / session Extrawurst | multi-focus polish |

Relevant: [`guest/mod/`](../include/pymergetic/metal/guest/mod/mod.h), [`wasm.h`](../include/pymergetic/metal/guest/wasm/wasm.h) (engine only), [`async_session.c`](../src/pymergetic/metal/runtime/async/async_session.c), [`process.h`](../include/pymergetic/metal/guest/process/process.h).

---

## Implementation checklist

1. **Mod registry** — **done**  
2. **Function + command registries** — **done**  
3. **Host natives** — **done**  
4. **Loader** `on_load` / `on_unload` only — **done**  
5. Resolve-once + `fn_coro` / `fn_process` — **done**  
6. Nestable process on task tree — **done**  
7. Opt-in fresh instance per process (restart/re-run/concurrent mods) — **done**  
8. AOT doom `#GP` (parallel) — **open**  
9. µPy binds the **same** registries — **done** (`pymergetic.metal.mod`, `guest/mod/mod_py_bind.c`)  

---

## Related

- Async ABI: `include/pymergetic/metal/runtime/async/async.h`, `docs/LIBC_ASYNC.md`  
- Doom app: `docs/DOOM_ASYNC.md`  
- µPy: `docs/MICROPYTHON.md` — binds land in `pymergetic.metal.mod` (`guest/mod/mod_py_bind.c`)  
- `docs/EFI.md`, `docs/IO.md`  
