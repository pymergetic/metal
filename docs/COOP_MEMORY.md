# Cooperative memory & CPU layout

Freestanding Metal (EFI first). Dual-span arena + stackless coro/task
(asyncio-shaped). See [EFI.md](EFI.md).

**Tree:** contracts in `src/pymergetic/metal/`; EFI binds in `src/efi/pymergetic/metal/`.

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
CPU k: MAP stack + inbox (heap, PM_METAL_MEM_ID_INBOX(k))
         pm_metal_run_enter → SwitchStack → drain until STOP
```

Messages: `PING` / `ADD` / `TASK` / `STOP`.  
`TASK` cookie = `pm_metal_task_t *` → `pm_metal_task_step`.  
Idle inbox → `pm_metal_coro_poll_timers` (coop sleep / wait_for).  
`create_task` round-robins across CPUs (no preferred CPU).  
`task_new` + `task_spawn(cpu)` places / migrates explicitly.  
Any looper may step any task pointer.

### Time

```c
pm_metal_time_usleep(us);   /* TSC busy-wait (MP-safe; no Boot Services) */
pm_metal_time_msleep(ms);
pm_metal_time_sleep(sec);
pm_metal_time_mono_us();    /* timer deadlines */
pm_metal_sleep(ms);         /* awaitable — coop */
```

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
- Imports (`include/pymergetic/metal/async.h`, module `pymergetic.metal.async`)
  arm host `sleep` / `yield` / tasks behind **uint32 handles** only.
- Await returns `WAITING` to the host runloop; `run_poll` + timer poll resumes
  the guest on wake. Sync mods without the export still use `execute_main`.
- Long-lived guests (e.g. doomgeneric) park every tick with `await(yield)`;
  `shell_poll` pumps until `DONE`. Only `wasi proc exit` (not bare traps) is
  treated as clean `DONE`. Game state is **one fiber**; Metal can schedule
  that task on any CPU with a live looper (this cut: CPU0 + shell pump).
  Multicore Doom would mean migrating the fiber / keeping AP loopers alive —
  not parallelizing the engine.

---

## Rules

1. Claim RAM once before ExitBootServices.
2. Maps vs heap: `pm_metal_mem_free` is TLSF only; maps use `pm_metal_mem_unmap` (LIFO).
3. Looper stacks on MAP; coros/tasks on HEAP.
4. Park = return to runloop (stackless steps); `yield` requeues on purpose.
