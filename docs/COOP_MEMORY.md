# Cooperative memory & CPU layout

Freestanding Metal (EFI first). Dual-span arena + stackless coro/task
(asyncio-shaped). See [EFI.md](EFI.md).

**Live tree:** [`src/.../mem/`](../src/pymergetic/metal/mem/) +
[`async/`](../src/pymergetic/metal/async/). Product-era `src/efi/...`
paths mean [`_old/src/efi/...`](../_old/src/efi/) unless noted.

---

## Dual-span arena

One claimed conventional hole (`EfiLoaderData`):

```text
low (map_brk →)                    (← heap_brk) high
[ pages / stacks / job grants ][ HOLE ][ TLSF pools ]
```

| Side | API | Grows |
|------|-----|--------|
| Low | `pm_metal_mem_map` / `PM_METAL_MEM_MAP` | upward |
| High | TLSF via `pm_metal_mem_alloc` (HEAP) | downward (new pools) |
| Middle | free hole | shrinks until OOM |

- **Looper stacks** come from the map side.
- **Coros / tasks** and general malloc use the heap (TLSF).
- No fixed LOCAL½ / SHARED¼ split. “Shared” is coop + messages, not a slab class.

---

## Front end

```c
void *pm_metal_mem_alloc(size_t n, pm_metal_mem_flags_t where, pm_metal_mem_id_t id);
void *pm_metal_mem_map(size_t n);
void *pm_metal_mem_lookup(pm_metal_mem_id_t id);
```

| `where` | Backend |
|---------|---------|
| `HEAP` / `LOCAL` / `SHARED` | TLSF (high side) |
| `MAP` | page map (low side) |

`id != 0` publishes into a circular list (nodes on the heap).

---

## Per-CPU runloop

```text
CPU k: MAP stack + ready ring (heap, PM_METAL_MEM_ID_INBOX(k))
         pm_metal_run_enter → SwitchStack → drain until STOP
```

**No `SPIN_LOCK` in the scheduler.** Each CPU owns a ring of tagged 64-bit
slots (`src/pymergetic/metal/runtime/run/run.c`): low 48 bits = payload
(a `pm_metal_task_t *`, or a small immediate for `ADD`/`PING`/`STOP`),
top bits = state (`EMPTY`/`READY`/`CLAIMED`) + kind. Push and claim are
each a single `InterlockedCompareExchange64` on one slot — there is no
lock to acquire even to look at a ring you don't own:

- **Push** (`pm_metal_run_post` / `pm_metal_run_post_ex`) CASes the first
  `EMPTY` slot in the target CPU's ring to `READY`. If that ring is full
  (rare — depth 64), it spills to a neighbor CPU's ring rather than
  dropping the message.
- **Claim** CASes a `READY` slot to `CLAIMED`; the claimant now owns that
  slot exclusively (only it may ever set it back to `EMPTY`, so the
  release is a plain store, no CAS needed).
- **Stealing is not a separate mechanism** — it's the same claim CAS run
  against a *foreign* ring. When a CPU's own ring is empty, `run_loop`
  tries the next CPU's ring before idling. `STOP` is never stolen (a
  stealer skips over `STOP`-tagged slots): `STOP` terminates a specific
  runner's own loop, so only that runner may claim its own.
- `pm_metal_run_steal_count(cpu)` / `pm_metal_run_claim_retries(cpu)` —
  diagnostics: how often another CPU claimed from `cpu`'s ring, and how
  many claim CASes lost the race (contention, not correctness).

`TASK` payload = `pm_metal_task_t *` → `pm_metal_task_step`.  
Idle ring (own *and* every neighbor tried) → `pm_metal_coro_poll_timers`.  
`create_task` round-robins across CPUs (no preferred CPU).  
`task_new` + `task_spawn(cpu)` places / migrates explicitly — `cpu` is a
hint for where the ring push starts, not a hard placement (spill / steal
can land or pick it up elsewhere).  
Any looper may step any task pointer.

### Sorted upcoming-timer list

`coro_timers.c`'s `mTimers` (sleep / `wait_for` deadlines) is kept
**sorted ascending by deadline** on arm (the one remaining `SPIN_LOCK` in
the runtime — a short insert/unlink on a list of at most a handful of
outstanding timers). `pm_metal_coro_poll_timers`, called from every idle
tick on every CPU, only ever peeks the **head**: if it's not due yet,
nothing after it is either, so it returns immediately instead of walking
the whole list. Firing a due timer hands its task pointer straight to the
ring push above — same mechanism as any other wake, no separate
"priority" path. A frame-pacing deadline (present/draw) is just another
sorted-list entry, not a third structure.

### Time

```c
pm_metal_time_usleep(us);   /* TSC busy-wait (MP-safe; no Boot Services) */
pm_metal_time_msleep(ms);
pm_metal_time_sleep(sec);
pm_metal_time_mono_us();    /* timer deadlines */
pm_metal_sleep(ms);         /* awaitable — coop (= sleep_us(ms*1000)) */
pm_metal_sleep_us(us);      /* awaitable µs */
pm_metal_sleep_until_us(t); /* absolute mono deadline */
```

**Runners:** N CPUs = N equal cooperative runners (FCFS), lock-free ready
rings + opportunistic stealing. No CPU0-only Extrawurst for wasm —
`create_task` round-robins; `run_poll_all` drains every ring. See `docs/IO.md`.

---

## Coro + Task (asyncio-shaped)

| Python | Metal |
|--------|--------|
| coroutine / awaitable | `pm_metal_coro_t` (`pm_metal_coro`) |
| `await x` | `pm_metal_await(self, x)` |
| `asyncio.create_task` | `pm_metal_create_task` |
| `await task` | `pm_metal_await_task` |
| `asyncio.sleep` | `pm_metal_sleep` |
| `await asyncio.sleep(0)` | `pm_metal_yield` |
| `asyncio.gather` | `pm_metal_gather` |
| `asyncio.wait_for` | `pm_metal_wait_for` |
| `asyncio.run` | `pm_metal_task_run` |

**Task** = independent flow (runner schedules it).  
**Coro** = nestable awaitable frame (`awaiting` / `waiter` chain).

```c
typedef struct {
  pm_metal_coro_t  coro;   /* must be first */
  uint32_t         step;
  uint32_t         in, out;
} my_coro_t;

my_coro_t *c = (my_coro_t *)pm_metal_coro(fn, sizeof(*c));
pm_metal_task_t *t = pm_metal_create_task(&c->coro); /* schedules */
pm_metal_task_spawn(t, other_cpu);                   /* continue / migrate */
```

Nested await:

```c
return pm_metal_await(self, child);   /* park; leaf driven on step */
```

- Know what you need at create (`sizeof` the composite).
- Heap allocs inside a step store pointers in those fields.
- Park = return `WAITING` / `PENDING` (stackless steps).

**Yield** (`pm_metal_yield`): scheduling fairness, not time. Posts the task
to the tail of its current CPU inbox, then parks. Other ready `TASK`s on
that CPU run first. No timer. Migrate with `pm_metal_task_spawn`.

```c
return pm_metal_await(self, pm_metal_yield());
```

### Guest async

Wasm guests share the same park model — not blocking natives, not Asyncify.

- Guest **exports** `i32 pm_metal_guest_step(i32 self_h)` (stackless switch on
  state in guest linear memory).
- Host wraps each fiber as a coro trampoline that `call_wasm`s that export.
- Imports (`include/pymergetic/metal/runtime/async/async.h`, module `pymergetic.metal.async`)
  arm host `sleep` / `yield` / tasks behind **uint32 handles** only.
- Await returns `WAITING` to the host runloop; `run_poll` + timer poll resumes
  the guest on wake. Sync mods without the export still use `execute_main`.
- Long-lived guests park every tick with `await(sleep|yield)`;
  `shell_poll` pumps until `DONE`. Only `wasi proc exit` (not bare traps) is
  treated as clean `DONE`.

#### No CPU pinning — a narrow call-in mutex instead

There is **no session-affinity/CPU-pinning concept**. A guest call-in is a
plain function call (`wasm_runtime_call_wasm`) — argument locations and
call-stack are the only thing tied to "where"; everything else is reached
through handles/pointers, which already works cross-CPU (that's the whole
point of being stackless). So Doom's `Tick`, blit, present, pace-sleep,
audio, and input tasks are independent tasks like any other — free to run
on any CPU, scheduled round-robin (`MetalPickCpu`), not forced to share a
runner.

The one thing that is genuinely shared, mutable, and must not race is the
process-wide WAMR `exec_env`/module instance itself. `MetalGuestCoroFn`
(`src/pymergetic/metal/runtime/async/async.c`) guards the actual call-in —
`wasm_runtime_call_wasm` plus the `mRunningCallin` / `wasm_bind_inst` set
that must stay atomic with it — behind a lightweight
`InterlockedCompareExchange32` mutex. Unlike the gfx present job (which can
safely drop a contended frame), a call-in **cannot be skipped**: on
contention the coro requeues itself (`pm_metal_await(self,
pm_metal_yield())`) and retries on its next step. `mSessionCpu` still
exists purely as a diagnostic (which CPU began the session; `cpu` shell
command, present-offload target base) — it is not a scheduling constraint.

Natives that only touch their own `exec_env` parameter (WAMR's standard
native calling convention) need no additional synchronization at all —
`wasm_runtime_get_module_inst(exec_env)` gives each one its own module
handle locally instead of relying on a shared `bind_inst`-style global.

The other genuinely-shared structure in `async.c` is the handle table
(`mSlots[]` — host fibers, host/guest coros, looked up by a small `uint32`
handle), now that guest-coro and host-fiber creation is no longer confined
to one CPU. Unlike the call-in mutex, this isn't one shared resource, it's
a *table* of independent ones — a single lock around the whole thing would
serialize every unrelated handle's lookup behind each other, the same
central-bottleneck problem the ready ring avoids. So each slot is instead
one lock-free tagged 64-bit word (payload + kind, exactly the ready ring's
trick): `MetalAsyncGet` is a single atomic load (no lock — it's the
hottest call here, every host-fiber/guest-coro poll goes through it),
`MetalAsyncAlloc` CASes a free (`0`) slot, `MetalAsyncClear` is a plain
aligned 64-bit store back to `0` (atomic on x86-64, so a concurrent `Get`
never observes a torn kind/ptr pair).

`async.c`'s table is not the only hand-rolled "scan an array for a free
slot" table in the tree — `process.c` (pid table) and `stream.c` (fd
table) have the exact same shape, just with bigger per-slot payloads
(names, ring buffers, ...) that don't fit in one 64-bit word the way
async's kind+ptr pair does. Both used to scan-and-write their `used`
flag with **no synchronization at all** — a real double-allocation race
now that spawning a process or opening a stream can happen from any
CPU. They now use a shared primitive,
`src/pymergetic/metal/runtime/slot/slot_table.h` (an internal, EDK2-typed
header — deliberately under `src/`, not `include/`, since `include/` is
this package's public API and nothing outside these runtime `.c` files
has any business calling it):
`pm_metal_slot_try_claim()` CASes a slot's leading `volatile UINT32` tag
from `0` to a caller-chosen non-zero value (so the *index* is never won
by two CPUs at once), `pm_metal_slot_claimed_zero()` then zeroes the
rest of the (now exclusively-owned) struct without touching the tag it
just set. Release paths that already hold the only reference (`close`,
`reap`) keep doing a plain whole-struct zero, same as before — only the
alloc-side scan-and-claim needed fixing. This is deliberately *not* a
generic templated container: each table still owns its own struct, size,
and scan loop; the header only factors out the CAS ticket that made the
scan safe, which is the part that's easy to get wrong and was actually
being duplicated three times.

---

## Rules

1. Claim RAM once before ExitBootServices.
2. Maps vs heap: `pm_metal_mem_free` is TLSF only; maps use `pm_metal_mem_unmap` (LIFO).
3. Looper stacks on MAP; coros/tasks on HEAP.
4. Park = return to runloop (stackless steps); `yield` requeues on purpose.
