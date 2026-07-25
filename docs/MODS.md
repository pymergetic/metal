# Mods, functions, commands, process (EFI product model)

**Status:** contract locked. Nestable process-on-task-tree landed; µPy bind
and multi-focus UI still open — see [Today vs target](#today-vs-target).

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
| **Function** | `func_resolve` → `fn_t` → `fn_coro` / task | other mods, host, **µPy later** |
| **Command** | `cmd_resolve` → `cmd_t` (`fn` first + `name`) → `fn_process` | **Shell** now; **µPy later** too |

Shell, **other mods**, and µPy later use the **same** registries / load API.
No second py-only table. **No string lookup on the hot path** after resolve.

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
7. AOT doom `#GP` (parallel) — **open**  
8. µPy binds the **same** registries — **next**  

---

## Related

- Async ABI: `include/pymergetic/metal/runtime/async/async.h`, `docs/LIBC_ASYNC.md`  
- Doom app: `docs/DOOM_ASYNC.md`  
- µPy: `docs/MICROPYTHON.md` — binds after registries are real  
- `docs/EFI.md`, `docs/IO.md`  
