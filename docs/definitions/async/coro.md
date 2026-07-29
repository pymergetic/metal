# Coro

## Definition

A **coro** is one **activation** of async work: a step function plus a
host-durable frame that can park (`await`).

It is not “the code” in the abstract (that is the shared step function).
It is not, by itself, something the runner polls. It is a nestable
awaitable frame in the hierarchy.

Every [task](task.md) and [process](process.md) **is** a coro (a
scheduled or intent-flagged one). Most coros are never tasks: they run
only because a parent awaited them.

## Relation to task

```text
task (on the runner)          ← runtime advances THIS
  └── step ...
        └── await child_coro  ← child is a coro, not scheduled
              └── await ...
```

- **Coro → coro:** direct nest. Parent parks on child; no second queue
  entry. The child progresses when the outer [task](task.md) is stepped.
- **Task:** the activation the runtime is responsible for advancing
  (see [task.md](task.md)).

A child coro does **not** need `create_task`. It runs because its
ancestor task was scheduled.

## Must

- Have a step that can return a park status (`await` / waiting).
- Keep state that survives a park in the durable frame (Metal heap /
  coro frame) — not on the C or wasm call stack across `await`.
- Allow nesting: `await` another coro; waiter/awaiting stay in one tree.

## Must not

- Mean “on the runner.” Unscheduled and nested-only coros are normal.
- Mean orchestration identity (pid, UI bind) — that is a
  [process](process.md) flag.
- Treat file-scope BSS as the durable store; the frame (and handles it
  holds) is the store.

## Rank

```text
coro  ⊂  task  ⊂  process
```

**Portion at this rank:** activation (step + frame + park/nest).  
**Not at this rank:** schedule.

## Related

- [task.md](task.md) — scheduled activation.
- API: `pm_metal_async_coro_create` / `coro_state` / `coro_alloc` /
  `await` (`include/pymergetic/metal/runtime/async/`).
- [`COOP_MEMORY.md`](../../COOP_MEMORY.md).
