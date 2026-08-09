# Orchestration — process, quit, boot / unboot

Product rules for intent roots and seat tear-down. Complements
[`definitions/async/`](definitions/async/README.md).

## Async step budget (sweet spots)

Cooperative: a coro step runs **uninterrupted** until it returns. Jitter ≈
longest step; switch cost ≈ ready pop/push + call (hundreds of cycles here).

| Regime | Step work | Effect |
|--------|-----------|--------|
| Too fine | ≪ 1 µs useful work | Switch-bound; meter avg_cyc ≈ overhead |
| Sweet | ~10–100 µs interactive; ~100 µs–1 ms batch | Useful work dominates; latency still bounded |
| Too coarse | multi-ms / blocking I/O in step | Visible jitter; starves READY_CAP peers |

Rules of thumb:

1. **Worst-case step ≤ latency budget / ready depth** you care about.
2. If meter `avg_cyc` ≈ empty-step baseline, coalesce work; if `max_cyc` spikes, split.
3. `PM_METAL_ASYNC_READY_CAP` caps steps per poll — long steps amplify unfairness inside that cap.
4. Meter with `pm_metal_async_meter_enable(1)` (RDTSC enter/exit around `step`); off = one predicted-false branch. Compile out: `-DPM_METAL_ASYNC_METER=0`.

## What is a process

A **process** is a **user-intent logical unit**: an orchestration face
someone (or a script/API) asked to run and may list, watch, or stop.
Promotion on the async engine: `coro ⊂ task ⊂ process` (intent flag +
watchable id). Not a second scheduler.

**Is a process (examples):**

- `sshd` when started via `net.ssh.listen` / public API
- `httpd` when ASGI/HTTP listen starts to serve

**Is not a process:**

- The **REPL** — local view on one µPy instance + multi runners
- Boot `_shell`, internal net life, drivers, mod-to-mod tasks
- Every scheduled task
- WASI guests as such (wasm = code container; unload = registry quiesce)

**Anti-patterns:** Linux-style shell command registry / `pmcmd` / process
argv packing. We are Python. `ps` is C/RS/Py on `pymergetic.metal.process`.

## Modes

| Mode | Meaning |
|------|---------|
| foreground | Owns interactive in/out while running |
| background | No console ownership; listed in `ps`; logs elsewhere |
| daemon | Long-lived service (sshd/httpd default) |

## Verbs

| Verb | Means |
|------|--------|
| `quit()` / `exit([code])` | End **current** process. No current → **noop + hint** |
| `quit(pid)` | Outside trigger for that pid |
| `unboot()` | Reverse boot: shutting_down → quit all procs → free seat |
| `shutdown()` | Shim: `unboot()` + halt/dead |
| `reboot()` | Shim: `unboot()` + reset/revive |
| Ctrl-D | Leave REPL / host face (not process quit) |

Never map `quit` → `unboot` / `shutdown` / `reboot`.

## Boot ↔ unboot

Same staged spine, opposite order (put shoes on / take them off):

```text
boot()     = staged bring-up
unboot()   = reverse spine (the work)
shutdown() = unboot() + halt/dead
reboot()   = unboot() + boot-again / revive
```

All seats share the drain; only the final hook differs (FW power, unix
leave host, browser dead + UI Reset).

## Cleanup

`quit` must tear down **tracelessly**: mark quitting, walk await-child
graph, call teardown callback, reclaim process + async slots. `ps` must
no longer list the pid.

## Faces

- C ABI: `pm_metal_process_*`, `pm_metal_boot_unboot` / `_shutdown` / `_reboot`
- RS: thin wrappers in `src/.../process/__init__.rs`, `boot/__init__.rs`
- Py: `pymergetic.metal.process`, `pymergetic.metal.boot`; builtins via `site.py`
