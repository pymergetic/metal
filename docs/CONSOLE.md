# Console & logging

Host-only I/O plumbing: how bytes get from the runtime's own diagnostics and
every loaded module's WASI stdio to a human at a real terminal (and later, a
network collector) — and back the other way for input, including the
command **shell** that turns typed lines into `load`/`run`/`unload`/... See
[RUNTIME.md](RUNTIME.md) "Console model" for how this plugs into the loader
API; [SOURCETREE.md](SOURCETREE.md) for where the files live.

---

## Three layers, one direction of dependency

```
util/log.h        formatting + one global capture floor — shared, guest-safe
      ↑
console/console.h  sinks — host-only, bidirectional pipes, no rendering
      ↑
console/viewport.h renders/multiplexes sinks — host-only, port-specific
```

Each layer only knows about the one below it. `log.h` has never heard of a
sink. `console.h` has never heard of a viewport. Nothing here is layered the
other way around.

### `util/log.h` — shared formatter + global floor

Pure C, zero OS dependency — the same "shared" exception as `util/size.h`,
`util/arena.h` (see [SOURCETREE.md](SOURCETREE.md) "shared"): safe for a mod
to call too, if a guest ever gets direct access to it. Two jobs, and only
two:

- **One global capture floor** (`pm_metal_util_log_set_level`/`get_level`,
  default `PM_METAL_LOG_INFO`) — below this, `pm_metal_util_log_write()` is a
  complete no-op, it doesn't even touch the `FILE*` it was given. One knob
  for the whole process — not per-sink, not per-pane. Not thread-safe against
  a `set_level()` racing a `write()` — deliberately: this is a coarse,
  rarely-changed knob meant to be set once at boot, before other threads
  exist (see "Log level: global floor vs per-pane filter" below).
- **Formatting** — `"[LEVEL] " + vsnprintf(fmt, ...) + "\n"`, `fflush()`d
  immediately, onto whatever `FILE*` the caller already has open. It does not
  know or care whether that `FILE*` is a console sink's `->out`, plain
  `stdout`/`stderr`, or (on a guest) WASI's own stdout — that's the caller's
  business.

Everything else in this document builds on top of this — `log.h` itself
never becomes aware of any of it.

### `console/console.h` — sinks

A **sink** is one full-duplex byte-pipe pair between a producer and a
consumer of a single console's worth of bytes. Two kinds, same struct, same
`open()`/`close()` — **kernel is a peer sink, not a special case**:

| Kind | Producer writes via | Consumer reads via |
|------|---------------------|---------------------|
| `PM_METAL_CONSOLE_KERNEL` | the host itself (`fprintf`/`pm_metal_util_log_write` onto `sink->out`) | the kernel command dispatcher (`fgets` on `sink->in`) |
| `PM_METAL_CONSOLE_HANDLE` | the guest's WASI fd 1/2, via `sink->producer_out_fd` handed to `run_ex()` as `stdout_fd`/`stderr_fd` | the guest's WASI fd 0, via `sink->consumer_in_fd` handed to `run_ex()` as `stdin_fd` |

`console.c` only allocates/frees the two underlying pipes (linux: `pipe(2)`,
4 fds total) and fills in the sink struct — it never calls into `runtime.h`
itself and knows nothing about focus, rendering, or multiplexing. A caller
still decides *when* a handle's module runs and passes that sink's fds to
`run_ex()` itself (see "`run_ex()`: the seam" below).

### `console/viewport.h` — render + focus + input routing

A **viewport** renders (and, for `LOCAL`, injects input into) a *set* of
registered sinks — it owns focus/multiplexing; `console.h` does not. Two
kinds, matrix-shaped against sinks (any sink may be registered with either or
both at once — see "Sinks × viewports" below):

| Kind | What it does | Status |
|------|---------------|--------|
| `PM_METAL_VIEWPORT_LOCAL` | one focused sink at a time, mirrored onto the real terminal; every *registered* sink keeps draining into a small backlog ring whether focused or not | implemented (linux) |
| `PM_METAL_VIEWPORT_NETWORK` | every registered sink, filtered, shipped to a remote collector — no focus concept, nothing to inject input into | stub — see "Not yet" |

Every registered sink's output is drained continuously (not just the
focused one) so a chatty unfocused module never blocks on a full pipe — the
alternative (only reading the focused sink) would let an unfocused guest's
`write()` to its own stdout stall once the pipe's kernel buffer fills, since
nothing would be reading the other end.

### What's common vs bind

Registration/focus/filter/ring/escape-byte bookkeeping in `viewport.c` is
pure struct-and-mutex logic with zero OS dependency — one shared
implementation (`src/common/.../console/viewport.c`) for every target, not a
per-plat bind, using `port/term.h` (a one-function primitive: "write to the
real local terminal") for the one genuinely OS-specific thing it still needs
(flushing a newly-focused sink's backlog onto the real terminal). Only
`pump()` itself — the actual `poll(2)`+`read(2)`/`write(2)` loop against
`console.h`'s sink fds and real stdin — stays a true per-target bind
(`src/linux/.../console/viewport.c`; zephyr's is still a stub). See
`src/common/.../console/viewport_local.h` (private — shared only between
that common core and each bind's `pump()`) for the exact seam between them.

---

## `run_ex()`: the seam

```c
int pm_metal_runtime_run_ex(pm_metal_runtime_handle_t h, int argc, char **argv, int envc, const char **envp,
			     int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd);
```

`run()` is `run_ex()` with `0, NULL` for env and `-1,-1,-1` for the fds —
the fd `-1`s are WAMR's own sentinel for "inherit the host process's real
fd 0/1/2" (`os_convert_std{in,out,err}_handle()` in WAMR's posix platform
code). `run_ex()` exists so a per-handle console can hand its own pipe
fds (and env) through to WASI instead — a caller opens a sink
(`console.h`), passes `sink->consumer_in_fd`/`producer_out_fd`/
`producer_out_fd` as `stdin_fd`/`stdout_fd`/`stderr_fd`, and everything
downstream of `wasm_runtime_set_wasi_args_ex()` behaves exactly as if that
fd really were the guest's stdio — because, from WASI's point of view, it
is. Nothing else in `runtime.c` needed to change to support this.

`app/app.h`'s `pm_metal_app_run_console()` (linux's `--console` mode) is
the reference caller, via `runtime/process.h` (see
[RUNTIME.md](RUNTIME.md) "Processes") rather than
calling `run_ex()` directly: each `run` command opens (at `load` time) a
`PM_METAL_CONSOLE_HANDLE` sink, registers it with the `LOCAL` viewport, and
`pm_metal_process_spawn()`s with that sink's fds — so a handle's
stdout/stderr always lands in its own pane, live if focused, backlogged if
not, across as many concurrent processes as that handle sees before
`unload()`.

---

## Sinks × viewports

A sink does not know which viewport(s), if any, are watching it — `console.c`
never calls into `viewport.h`. Registration is one-directional, from the
viewport's side (`pm_metal_viewport_register(kind, sink)`), and a single sink
may be registered with more than one viewport kind at once — e.g. once with
`LOCAL` (so a human can focus it at a real terminal) and, later, once with
`NETWORK` too (so the same bytes also ship to a remote collector) — without
either viewport knowing about the other. This is why the matrix shape
matters: sinks and viewports are orthogonal, not nested.

---

## Log level: global floor vs per-pane filter

Two different, independently-set knobs, easy to conflate, kept apart on
purpose:

- **`util/log.h`'s global floor** — set once, decides what's even worth
  *keeping* at all, process-wide. A message below this floor is gone before
  it ever reaches a sink — no pane, no viewport, no backlog ring, will ever
  see it, because `pm_metal_util_log_write()` never wrote it in the first
  place.
- **`viewport.h`'s per-pane filter** (`pm_metal_viewport_set_filter(kind,
  sink, floor)`) — a display-time filter on bytes that already made it past
  the global floor and are sitting in that sink's own ring. Two different
  panes can show different slices of the *same* underlying capture — e.g.
  the kernel pane always visible at `INFO`, while a noisy handle's pane is
  set to `WARN` so its `INFO`-level chatter is skipped when rendering, without
  ever having discarded it at the source.

Put differently: the global floor answers "is this worth capturing at all,
anywhere?" once, for the whole process. The per-pane filter answers "is this
worth *showing*, in this one pane, right now?" — and can be changed at any
time without losing anything the global floor already let through.

---

## The escape byte (LOCAL, linux)

Once focus is on a handle, ordinary typed input goes straight to *that
module's* WASI stdin — including the literal text `list` or `quit`, which a
guest reading stdin would just receive as input, not a command. There would
be no way back to the kernel pane at all otherwise. `viewport.h`'s contract
reserves *some* escape mechanism for this on every `LOCAL` bind, without
mandating which byte — it depends on what "the real terminal" even is on
that port (a tty escape byte vs., say, a dedicated SDL hotkey later).

The linux bind uses **Ctrl-A** (`PM_METAL_VIEWPORT_ESCAPE_BYTE`, `0x01`),
intercepted in `pump()` itself, before any forwarding: seeing it anywhere as
the first byte of a chunk read from real stdin switches focus straight back
to slot 0 (kernel, by this codebase's registration-order convention — see
`app/app.c`'s `pm_metal_app_run_console()`) and is consumed, never forwarded to any sink; anything
after it in the same chunk is forwarded normally, to the (now newly-focused)
sink. Proven end-to-end in `scripts/verify-linux-console.sh`.

---

## Kernel is a peer, not special

`PM_METAL_CONSOLE_KERNEL` uses the exact same `pm_metal_console_sink_t`
struct, the exact same `open()`/`close()`, and registers with the exact same
`pm_metal_viewport_register()` as any handle's sink — `app/app.c`'s
`pm_metal_app_run_console()` just happens to open it first (slot 0) and
never `unload()`s it. This
symmetry is what let the escape byte's "back to slot 0" rule stay this
simple, and what will let the kernel's own diagnostics ship to a `NETWORK`
viewport later with zero special-casing.

---

## Shell

The kernel console's dispatcher does not hand-parse an `if`/`else` chain of
command names — it calls one function, `pm_metal_shell_dispatch_line()`
(`src/common/.../shell/shell.h`), which resolves argv[0] and either invokes a
native handler directly or spawns-and-waits a `.wasm` file (via
`runtime/process.h`, see [RUNTIME.md](RUNTIME.md) "Processes"), uniformly.
impl: common — nothing in `shell.c`/`commands.c`/`commands/*.c` is
OS-specific (workers go through `runtime/process.h`, which itself goes
through `port/worker.h`
rather than raw `pthread_*`, and `ls`/`cd` use `port/dir.h` instead of raw
`opendir`/`readdir`), so the same command set is reachable from any future
target's own console entry point, not just linux's.

### Registry + resolution order

A **native command** is a C function registered once via
`pm_metal_shell_register()` — `name`, `pm_metal_shell_cmd_fn`, `help` text.
Every builtin (`cd`, `env`, `exit`, `export`, `focus`, `help`, `load`, `ls`,
`ps`, `pwd`, `quit`, `run`, `unload`) is just an entry in this registry;
there is nothing structurally different about a "builtin" versus a
command some other module registers later. Each builtin's own
implementation lives in its own `shell/commands/<name>.{h,c}` pair — a
plain `pm_metal_shell_cmd_fn`, directly `#include`-able and callable by
anything holding a `pm_metal_shell_ctx_t`, not reachable only through this
registry. `shell/commands.c` just collects all of them into one
`pm_metal_shell_builtins_ops_t` (see `commands.h`) and registers each by
name off of that struct.

A bare word (e.g. `ls`) resolves in this order (`pm_metal_shell_resolve()`):

1. **wasm override** — `pm_metal_runtime_load_file("<PM_METAL_SHELL_BIN_DIR>/<name>.wasm")`
   (i.e. `/bin/<name>.wasm` in the guest-facing `vfs_root`). Checked with a
   quiet existence probe first (`pm_metal_port_file_exists()`) — expected to
   miss almost every time, so this path must never log anything on a miss
   (see `shell.c`'s own comment on this — an earlier version of this code
   spammed a `read failed` line for *every* ordinary command before this
   probe was added). If a real file is there, it wins — even over a
   same-named native command.
2. **native registry** — exact name match, if no override exists.

Never cached: dropping a real `ls.wasm` into `vfs_root/bin/` (e.g. a future
busybox-style set of real command binaries) starts shadowing the native `ls`
on the very next call, no restart needed.

Two explicit path forms bypass this order entirely:

| Form | Meaning |
|------|---------|
| `/bin/pm/<name>` | Virtual — native registry *only*, never shadowed. The escape hatch: reaches a builtin even if a `.wasm` override exists for the same bare name. |
| `/bin/<name>.wasm` (or `/bin/<name>`, `.wasm` appended if missing) | Real — an explicit override reference; no native fallback if it doesn't parse. |

`ls /bin/pm` and `ls`'s virtual-directory special case list the native
registry itself (there is no real directory backing `/bin/pm` — it is purely
the registry, rendered as if it were one); listing real `.wasm` files under
`/bin` goes through `port/dir.h` like any other directory.

### Working directory

Each console has its own `pm_metal_shell_ctx_t` (`sink` + `cwd`), starting at
`/`. `cd`/`pwd`/`ls` navigate `cwd` purely lexically
(`pm_metal_shell_resolve_path()` — joins + normalizes `.`/`..`, never
touching the real filesystem itself); `cd` additionally validates the
*target* is a real, listable `vfs_root` directory (via `port/dir.h`) before
committing to it, except the virtual `/bin/pm`, which always succeeds. `load`
and any other path-taking builtin resolve a relative argument against `cwd`
the same way, before handing the result to `pm_metal_runtime_load_file()`
(which then applies its own, separate `vfs_root`-relative resolution — `cwd`
is a shell-side concept on top of that, not a runtime one). Command name
resolution itself (§ above) does not consult `cwd` at all — a bare word is
found the same way no matter where you are, exactly like a real shell's
`$PATH` search.

### Dispatching a `.wasm` command

`pm_metal_shell_dispatch_line()`'s wasm branch is a synchronous, foreground
`pm_metal_process_spawn()` + `pm_metal_process_wait()` + `unload()` — it
blocks the dispatching console until the command finishes (same as a real
shell running a foreground command), using that console's own sink fds and
its own exported env (`pm_metal_shell_env_snapshot()`) for the guest, and
always `unload()`s afterward, win or lose. `argv[0]` is exactly as typed
(e.g. `ls`), not the resolved `/bin/ls.wasm` path — matching a real
program's own `argv[0]` convention. This is different from the `run <id>`
builtin, which runs an *already-loaded, still-resident* handle via the same
`process.h` `spawn()` but returns immediately (no `wait()`) so the kernel
prompt stays responsive — a `.wasm` command is load-spawn-wait-unload in
one shot, with no handle for `ps`/`focus`/`unload` to ever see. Both paths
go through the same `process.h` layer purely for uniformity — every guest
execution this codebase starts, foreground or backgrounded, is a tracked
process the same way (see [RUNTIME.md](RUNTIME.md) "Processes").

---

## Not yet

- **`PM_METAL_VIEWPORT_NETWORK`** — the interface exists (`init`/`pump`
  compile and return cleanly) so a real implementation is purely additive
  later; today `init()` and `pump()` are no-ops on every target. Zephyr
  natively supports RFC 5424 syslog in its own logging subsystem, which is
  the natural fit for a zephyr `NETWORK` bind; linux has no equivalent
  built in and would need a small manual UDP/TCP syslog client. Guest
  modules never get direct access to this — only sinks (kernel or handle)
  are ever registered with it, same as `LOCAL`.
- **Per-pane filter persistence** — `set_filter()` exists and works, but no
  CLI command exposes it yet (there is no `filter <id> <level>` kernel
  command); it is only reachable by a caller compiled into `app/app.c`
  today.
- **zephyr `console`/`pump()`** — stubbed (`src/zephyr/.../console/console.c`,
  and `viewport.c`'s `pump()` — see "What's common vs bind" above;
  registration/focus/filter themselves already work on every target),
  deferred with the rest of zephyr bring-up (see [RUNTIME.md](RUNTIME.md)
  "Bring-up plan" §5). `console.c`'s real implementation there needs a
  bidirectional byte-pipe primitive — Zephyr's own `k_pipe` is the natural
  fit, though unlike linux's `pipe(2)` it has no fd, so callers would go
  through `k_pipe_get()`/`put()` directly rather than `FILE*`/fd fields.
  `shell.c`/`commands.c`/`commands/*.c`/`app/app.c` are already fully
  `impl: common` and work unmodified once a target has a working
  console/pump — only `port/worker.h`
  (currently a stub — needs a real `k_thread` + stack) and `port/dir.h`
  (currently a stub — needs `fs_opendir()`/`fs_readdir()`,
  `CONFIG_FILE_SYSTEM`) are still unimplemented there.
- **Listing real `.wasm` overrides** — `pm_metal_shell_list_commands()`
  only enumerates the native registry; there is no equivalent "list every
  `.wasm` under `/bin`" helper in `shell.h` itself (though `ls /bin` gets you
  there today via `port/dir.h` directly).

---

## Reference implementation (linux)

| Piece | File |
|-------|------|
| formatter + global floor | `include/pymergetic/metal/util/log.h` (+ `log_impl.h`, loaded by `src/shared/.../util/log.c`) |
| sinks | `src/common/.../console/console.h`, `src/linux/.../console/console.c` |
| viewport (registration/focus/filter — impl: common) | `src/common/.../console/{viewport.h,viewport.c,viewport_local.h}` |
| viewport (`pump()` — impl: bind) | `src/linux/.../console/viewport.c` |
| terminal-write primitive | `src/common/.../port/term.h`, `src/linux/.../port/term.c` |
| worker-thread primitive | `src/common/.../port/worker.h`, `src/linux/.../port/worker.c` |
| directory-listing primitive | `src/common/.../port/dir.h`, `src/linux/.../port/dir.c` |
| processes (decoupled from handles, env) | `src/common/.../runtime/process.h`, `process.c` — see [RUNTIME.md](RUNTIME.md) "Processes" |
| shell registry/resolver/dispatch/cwd/env | `src/common/.../shell/shell.h`, `shell.c` |
| shell builtins ops struct | `src/common/.../shell/commands.h`, `commands.c` |
| shell builtins (one pair/command) + handle table | `src/common/.../shell/commands/*.{h,c}` |
| console-mode run loop (kernel sink, pump/dispatch-thread handshake — impl: common) | `src/common/.../app/app.h`, `app.c` |
| console-mode CLI (argv parsing only) | `src/linux/main.c` (`--console`) |
| proof | `scripts/verify-linux-console.sh` |

Console mode commands (`help` at the prompt): `load <path>`, `run <id>
[args...]`, `unload <id>`, `focus <id|kernel>`, `ps`, `cd [path]`, `pwd`,
`ls [path]`, `export KEY=VALUE`, `env`, `quit`/`exit` — plus any `.wasm`
override dropped into `vfs_root/bin/` (see "Shell" above), and every
builtin's explicit `/bin/pm/<name>` escape-hatch form. `run` spawns one
process per call (`runtime/process.h` — so the kernel prompt stays
responsive while a mod executes, and the *same* handle may have more than
one `run` in flight at once, each its own `pid`); `ps` lists both the loaded
handles and every tracked process against them. `unload`/`quit` never join
anything themselves — the runtime's own busy-refcount guard (see
[RUNTIME.md](RUNTIME.md) "Concurrency") already refuses `unload()` while any
process is still in flight against that handle, and `quit`'s teardown path
(`pm_metal_process_shutdown()`, see `commands.h`) joins everything globally,
by pid, before any handle's `unload()` runs.
