# Task

## Definition

A **task** is a [coro](coro.md) that is **on a runner**: the runtime
advances it as a unit of scheduled work.

Same activation (step + frame + `await`) as any coro. The promotion is
schedule — a queue identity and cooperative turns — not a second kind of
computation.

## Relation to coro

```text
step function (shared code)
        │
        ▼
coro  =  one activation (frame + step); may only be nested
        │
        ▼
task  =  that coro, actually run by the scheduler
```

| | Coro | Task |
|--|------|------|
| Activation (frame + step) | yes | yes (is a coro) |
| May `await` another coro | yes | yes |
| Own runner queue entry | no (unless also a task) | yes |
| How it progresses | parent’s step after nest, or becomes a task | runtime polls/steps it |

Nested work stays **coros**. Only roots (or coros explicitly promoted
with `create_task`) are **tasks**. The graph behind a task can be deep;
the scheduler still only needs the task.

## Must

- Be a coro.
- Be eligible for progress without the caller busy-looping its step.
- Overlap fairly with other tasks on the cooperative runtime.

## Must not

- Be required for every `await`. Child coros nest under a task without
  each becoming a task.
- Mean the orchestration face of a bundle — that is a
  [process](process.md) (intent flag + watched id).
- Mean a wasm cage / PURE vs SESSION. Code mode is a separate axis.

## Rank

```text
coro  --promote (schedule)-->  task  --promote (intent)-->  process
```

**Portion at this rank:** schedule (runner entry, cancel/status
identity, fair turn).

## Call-site note

The same activation may be a plain task when started from libraries or
another program, and may be promoted to a [process](process.md) when the
orchestration layer (shell, script, public API) crowns it as the public
face — without changing the step body.

## Related

- [coro.md](coro.md) — activation and nesting without schedule.
- [process.md](process.md) — intent flag + id on a task.
- API: `pm_metal_async_create_task` (`include/pymergetic/metal/runtime/async/`).
